import path from 'node:path'

import {auditCurrentPage, navigateAndCapture, startQaSession} from '../lib/browser.mjs'
import {
  runLocalExtensionLifecycle,
  runWebStoreExtensionLifecycle,
} from '../lib/extensions.mjs'
import {loadProxyFixtures, publicProxyRecord} from '../lib/fixtures.mjs'
import {
  collectProbe,
  collectWebRtcCandidates,
  readProfileProxyState,
  renderProxyValidationError,
  runProfileLifecycle,
  setProfileProxy,
  waitForProfileProxyError,
} from '../lib/profile.mjs'
import {runScenario} from '../lib/report.mjs'
import {
  captureNativeScreenshot,
  pngDimensions,
  run,
  setFrontWindowSize,
} from '../lib/system.mjs'
import {importNativeEvidence} from '../lib/native_evidence.mjs'

const GOOGLE_TRANSLATE_URL =
  'https://chromewebstore.google.com/detail/google-translate/aapbdbdomjkkjkaonfhkkikfgjllcleb'

function normalizeLanguage(value) {
  return String(value).toLowerCase().replace('_', '-')
}

function observedIpFromBody(body) {
  try {
    const parsed = JSON.parse(body)
    return parsed.ip || parsed.query || parsed.origin
  } catch {
    return body.trim()
  }
}

function observedGeoFromBody(body) {
  const parsed = JSON.parse(body)
  const countryCode = parsed.countryCode || parsed.country_code
  if (!countryCode || !parsed.timezone) {
    throw new Error('Geo verification response requires countryCode/country_code and timezone')
  }
  return {countryCode: String(countryCode).toUpperCase(), timezone: parsed.timezone}
}

async function assertCurrentWebUi(page, label) {
  const audit = await auditCurrentPage(page)
  if (audit.componentContrastFailures.length > 0 ||
      audit.contrastFailures.length > 0 || audit.layoutFailures.length > 0) {
    throw Object.assign(new Error(`${label} failed WebUI checks`), {
      details: audit,
    })
  }
  return audit
}

async function profilePrefs(page, updates = null) {
  await page.goto('brave://settings/privacy', {waitUntil: 'domcontentloaded'})
  return await page.evaluate(async updates => {
    if (updates) {
      for (const [key, value] of Object.entries(updates)) {
        await chrome.settingsPrivate.setPref(key, value)
      }
    }
    const result = {}
    for (const key of ['intl.accept_languages', 'webrtc.ip_handling_policy']) {
      result[key] = (await chrome.settingsPrivate.getPref(key)).value
    }
    return result
  }, updates)
}

async function verifyProxy({config, dirs, fixture, probe, runId}) {
  const profilePath = `/tmp/fingerprint-browser-${runId}/proxy-${fixture.scheme}`
  const events = []
  let session = await startQaSession({
    app: config.app,
    logDir: dirs.logs,
    name: `proxy-${fixture.scheme}`,
    profilePath,
  })
  events.push(session.events)
  try {
    let page = session.context.pages()[0] || await session.context.newPage()
    const originalPrefs = await profilePrefs(page, {
      'intl.accept_languages': 'fr-FR,fr',
      'webrtc.ip_handling_policy': 'default_public_and_private_interfaces',
    })
    if (originalPrefs['intl.accept_languages'] !== 'fr-FR,fr' ||
        originalPrefs['webrtc.ip_handling_policy'] !==
          'default_public_and_private_interfaces') {
      throw Object.assign(new Error('Could not establish restoration sentinels'), {
        details: originalPrefs,
      })
    }
    const defaultState = await readProfileProxyState(page)
    if (defaultState.enabled) {
      throw new Error(`${fixture.scheme} proxy was unexpectedly enabled by default`)
    }
    const defaultScreenshot = path.join(
      dirs.page, `full-proxy-${fixture.scheme}-default.png`)
    await page.screenshot({path: defaultScreenshot, fullPage: false})
    await assertCurrentWebUi(page, `${fixture.scheme} proxy default state`)
    const invalidScreenshot = path.join(
      dirs.page, `full-proxy-${fixture.scheme}-validation-error.png`)
    const validation = await renderProxyValidationError(page, invalidScreenshot)
    await assertCurrentWebUi(page, `${fixture.scheme} proxy validation`)

    const unknownGeo = await setProfileProxy(page, {
      ...fixture,
      countryCode: '',
      enabled: true,
      latitude: undefined,
      longitude: undefined,
      timezone: '',
    })
    if (!unknownGeo.geoWarning) {
      throw new Error(`${fixture.scheme} proxy did not show missing Geo warning`)
    }
    const geoWarningScreenshot = path.join(
      dirs.page, `full-proxy-${fixture.scheme}-geo-warning.png`)
    await page.screenshot({path: geoWarningScreenshot, fullPage: false})
    await assertCurrentWebUi(page, `${fixture.scheme} proxy Geo warning`)

    const save = await setProfileProxy(page, {
      ...fixture,
      enabled: true,
    })
    if (save.hostError || save.portError || save.saveError || !save.savedStatus) {
      throw new Error(`Could not save ${fixture.scheme} proxy: ${JSON.stringify(save)}`)
    }
    const savedScreenshot = path.join(
      dirs.page, `full-proxy-${fixture.scheme}-saved.png`)
    await page.screenshot({path: savedScreenshot, fullPage: false})
    await assertCurrentWebUi(page, `${fixture.scheme} proxy saved state`)

    const response = await page.goto(fixture.verifyUrl, {
      timeout: 60000,
      waitUntil: 'domcontentloaded',
    })
    if (!response?.ok()) {
      throw new Error(`${fixture.scheme} proxy verification returned ${response?.status()}`)
    }
    const body = await page.locator('body').innerText()
    const observedIp = observedIpFromBody(body)
    if (observedIp !== fixture.expectedIp) {
      throw new Error(
        `${fixture.scheme} exit IP mismatch: ${observedIp} != ${fixture.expectedIp}`)
    }
    const geoResponse = await page.goto(fixture.geoVerifyUrl, {
      timeout: 60000,
      waitUntil: 'domcontentloaded',
    })
    if (!geoResponse?.ok()) {
      throw new Error(`${fixture.scheme} Geo verification returned ${geoResponse?.status()}`)
    }
    const networkGeo = observedGeoFromBody(await page.locator('body').innerText())
    if (networkGeo.countryCode !== fixture.countryCode ||
        networkGeo.timezone !== fixture.timezone) {
      throw Object.assign(new Error(`${fixture.scheme} exit Geo mismatch`), {
        details: {expected: publicProxyRecord(fixture), networkGeo},
      })
    }

    await page.context().grantPermissions(['geolocation'], {origin: probe.origin})
    const observed = await collectProbe(page, probe.origin)
    if (observed.timezone !== fixture.timezone) {
      throw new Error(
        `${fixture.scheme} timezone mismatch: ${observed.timezone} != ${fixture.timezone}`)
    }
    const languages = observed.basic.languages.map(normalizeLanguage)
    if (!languages.some(language => language.startsWith(normalizeLanguage(fixture.language)))) {
      throw new Error(`${fixture.scheme} language mismatch: ${languages.join(', ')}`)
    }
    if (!Number.isFinite(observed.geolocation.latitude) ||
        !Number.isFinite(observed.geolocation.longitude)) {
      throw new Error(`${fixture.scheme} geolocation was unavailable`)
    }
    const latitudeDelta = Math.abs(observed.geolocation.latitude - fixture.latitude)
    const longitudeDelta = Math.abs(observed.geolocation.longitude - fixture.longitude)
    if (latitudeDelta > 0.1 || longitudeDelta > 0.1) {
      throw new Error(`${fixture.scheme} geolocation mismatch`)
    }

    const candidates = await collectWebRtcCandidates(page)
    const leakingCandidates = candidates.filter(candidate => {
      const srflx = candidate.includes(' typ srflx ')
      return srflx && !candidate.includes(fixture.expectedIp)
    })
    if (leakingCandidates.length > 0) {
      throw Object.assign(new Error(`${fixture.scheme} WebRTC leaked a non-proxy srflx address`), {
        details: {candidates, leakingCandidates},
      })
    }

    await session.close()
    session = await startQaSession({
      app: config.app,
      logDir: dirs.logs,
      name: `proxy-${fixture.scheme}-restart`,
      profilePath,
    })
    events.push(session.events)
    page = session.context.pages()[0] || await session.context.newPage()
    const persisted = await readProfileProxyState(page)
    if (!persisted.enabled || persisted.scheme !== fixture.scheme ||
        persisted.host !== fixture.host || persisted.port !== fixture.port) {
      throw Object.assign(new Error(`${fixture.scheme} proxy did not persist across restart`), {
        details: persisted,
      })
    }
    const restartedResponse = await page.goto(fixture.verifyUrl, {
      timeout: 60000,
      waitUntil: 'domcontentloaded',
    })
    if (!restartedResponse?.ok()) {
      throw new Error(`${fixture.scheme} proxy failed after restart`)
    }
    const restartedBody = await page.locator('body').innerText()
    if (!restartedBody.includes(fixture.expectedIp)) {
      throw new Error(`${fixture.scheme} exit IP changed after restart`)
    }

    await setProfileProxy(page, {
      ...fixture,
      enabled: true,
      password: `${fixture.password}-intentionally-wrong`,
    })
    await page.goto(fixture.verifyUrl, {
      timeout: 20000,
      waitUntil: 'domcontentloaded',
    }).catch(() => null)
    const authState = await waitForProfileProxyError(page)
    const authScreenshot = path.join(
      dirs.page, `full-proxy-${fixture.scheme}-authentication-error.png`)
    await page.screenshot({path: authScreenshot, fullPage: false})
    await assertCurrentWebUi(page, `${fixture.scheme} proxy authentication error`)

    await setProfileProxy(page, {
      ...fixture,
      enabled: false,
      host: '',
      port: 0,
      username: '',
      password: '',
      countryCode: '',
      timezone: '',
      latitude: undefined,
      longitude: undefined,
    })
    const direct = await page.goto(probe.origin, {waitUntil: 'domcontentloaded'})
    if (!direct?.ok()) {
      throw new Error('Direct settings were not restored after disabling proxy')
    }
    const restoredPrefs = await profilePrefs(page)
    if (JSON.stringify(restoredPrefs) !== JSON.stringify(originalPrefs)) {
      throw Object.assign(new Error('Language or WebRTC preference was not restored'), {
        details: {originalPrefs, restoredPrefs},
      })
    }
    const failures = events.flatMap(eventSet => [
      ...eventSet.browserExits.map(event => `browser exited ${event.code ?? event.signal}`),
      ...eventSet.crashes.map(event => `renderer crash ${event.page}`),
      ...eventSet.pageErrors.map(event => `pageerror ${event.message}`),
      ...eventSet.disconnected.map(() => 'CDP disconnected'),
    ])
    if (failures.length > 0) {
      throw Object.assign(new Error(failures.join('; ')), {details: events})
    }
    return {
      authState,
      candidates,
      fixture: publicProxyRecord(fixture),
      observed: {
        geolocation: observed.geolocation,
        ip: observedIp,
        languages: observed.basic.languages,
        timezone: observed.timezone,
      },
      networkGeo,
      persisted,
      restoredPrefs,
      screenshots: [
        defaultScreenshot,
        invalidScreenshot,
        geoWarningScreenshot,
        savedScreenshot,
        authScreenshot,
      ],
      validation,
    }
  } finally {
    await session.close()
  }
}

async function verifyProxySwitchPersistence({config, dirs, fixtures, probe, runId}) {
  const profilePath = `/tmp/fingerprint-browser-${runId}/proxy-switch`
  let session = await startQaSession({
    app: config.app,
    logDir: dirs.logs,
    name: 'proxy-switch',
    profilePath,
  })
  try {
    let page = session.context.pages()[0] || await session.context.newPage()
    const transitions = []
    for (const fixture of [fixtures.http, fixtures.socks5]) {
      const saved = await setProfileProxy(page, {...fixture, enabled: true})
      if (!saved.savedStatus) {
        throw new Error(`Could not switch to ${fixture.scheme}`)
      }
      const response = await page.goto(fixture.verifyUrl, {
        timeout: 60000,
        waitUntil: 'domcontentloaded',
      })
      const ip = response?.ok()
        ? observedIpFromBody(await page.locator('body').innerText())
        : null
      if (ip !== fixture.expectedIp) {
        throw new Error(`${fixture.scheme} switch exit IP mismatch`)
      }
      transitions.push({ip, scheme: fixture.scheme})
    }
    const screenshot = path.join(dirs.page, 'full-proxy-switch-socks5.png')
    await page.goto('brave://settings/privacy', {waitUntil: 'domcontentloaded'})
    await page.screenshot({path: screenshot, fullPage: false})
    await assertCurrentWebUi(page, 'SOCKS5 switch state')

    await session.close()
    session = await startQaSession({
      app: config.app,
      logDir: dirs.logs,
      name: 'proxy-switch-restart',
      profilePath,
    })
    page = session.context.pages()[0] || await session.context.newPage()
    const persisted = await readProfileProxyState(page)
    if (!persisted.enabled || persisted.scheme !== 'socks5') {
      throw new Error('Switched SOCKS5 proxy did not persist across restart')
    }
    await page.context().grantPermissions(['geolocation'], {origin: probe.origin})
    const observed = await collectProbe(page, probe.origin)
    await setProfileProxy(page, {
      countryCode: '',
      enabled: false,
      host: '',
      latitude: undefined,
      longitude: undefined,
      password: '',
      port: 0,
      scheme: 'socks5',
      timezone: '',
      username: '',
    })
    return {
      persisted,
      screenshots: [screenshot],
      transitions,
      workerContexts: {
        dedicatedWorker: observed.dedicatedWorker,
        serviceWorker: observed.serviceWorker,
        sharedWorker: observed.sharedWorker,
      },
    }
  } finally {
    await session.close()
  }
}

async function runThirdPartyScans({config, dirs, runId}) {
  const session = await startQaSession({
    app: config.app,
    logDir: dirs.logs,
    name: 'fingerprint-scans',
    profilePath: `/tmp/fingerprint-browser-${runId}/scans`,
  })
  const scans = [
    ['creepjs', 'https://abrahamjuliot.github.io/creepjs/'],
    ['fingerprintjs', 'https://fingerprint.com/demo/'],
    ['browserleaks', 'https://browserleaks.com/'],
    ['browserleaks-ssl', 'https://browserleaks.com/ssl'],
  ]
  try {
    const page = session.context.pages()[0] || await session.context.newPage()
    const evidence = []
    for (const [name, url] of scans) {
      const response = await page.goto(url, {timeout: 60000, waitUntil: 'domcontentloaded'})
      await page.waitForTimeout(5000)
      const screenshot = path.join(dirs.page, `full-scan-${name}.png`)
      await page.screenshot({path: screenshot, fullPage: false})
      evidence.push({name, screenshot, status: response?.status() ?? null, url: page.url()})
    }
    return {evidence, screenshots: evidence.map(item => item.screenshot)}
  } finally {
    await session.close()
  }
}

async function verifyTlsSourceScope(config) {
  const mergeBase = await run(
    'git', ['merge-base', 'HEAD', 'upstream/master'],
    {check: true, cwd: config.braveRoot})
  const base = mergeBase.stdout.trim()
  const diff = await run('git', [
    'diff', '--name-only', base, '--',
    'chromium_src/net/socket/ssl_client_socket_impl.cc',
    'chromium_src/third_party/boringssl',
    'patches/*boringssl*',
    'patches/*ssl_client_socket*',
  ], {check: true, cwd: config.braveRoot})
  const changed = diff.stdout.trim().split('\n').filter(Boolean)
  if (changed.length > 0) {
    throw Object.assign(new Error('TLS implementation differs from upstream base'), {
      details: {base, changed},
    })
  }
  return {base, changed}
}

export async function runUiMatrix({config, dirs, probe, runId}) {
  const sizes = [
    {height: 768, width: 1024},
    {height: 800, width: 1280},
    {height: 982, width: 1512},
  ]
  const states = [
    ['new-tab', 'brave://newtab/'],
    ['google', 'https://www.google.com/'],
    ['facebook', 'https://www.facebook.com/'],
    ['settings', 'brave://settings/'],
    ['fingerprint', 'brave://fingerprint-test/'],
  ]
  const screenshots = []
  for (const theme of ['light', 'dark']) {
    const session = await startQaSession({
      app: config.app,
      extraArgs: theme === 'dark' ? ['--force-dark-mode'] : [],
      logDir: dirs.logs,
      name: `ui-${theme}`,
      profilePath: `/tmp/fingerprint-browser-${runId}/ui-${theme}`,
      profilePreferences: {
        brave: {dark_mode_migrated: true},
        browser: {theme: {color_scheme2: theme === 'dark' ? 2 : 1}},
      },
    })
    try {
      const page = session.context.pages()[0] || await session.context.newPage()
      await page.emulateMedia({colorScheme: theme})
      for (const size of sizes) {
        await setFrontWindowSize(size.width, size.height, session.process.child.pid)
        await page.setViewportSize(size)
        for (const [name, url] of states) {
          let targetPage = page
          if (name === 'fingerprint') {
            await page.goto(probe.origin, {waitUntil: 'domcontentloaded'})
            targetPage = await session.context.newPage()
            await targetPage.setViewportSize(size)
            await targetPage.emulateMedia({colorScheme: theme})
          }
          const pageScreenshot = path.join(
            dirs.page, `full-${theme}-${name}-${size.width}x${size.height}.png`)
          const result = await navigateAndCapture({
            colorScheme: theme,
            page: targetPage,
            screenshot: pageScreenshot,
            url,
            validateContrast: name === 'fingerprint' || name === 'settings',
            validateLayout: name === 'fingerprint' || name === 'settings',
          })
          if (result.componentContrastFailures.length > 0 ||
              result.contrastFailures.length > 0 || result.layoutFailures.length > 0) {
            throw Object.assign(new Error(`${theme} ${name} ${size.width}x${size.height} failed UI checks`), {
              details: {
                componentContrastFailures: result.componentContrastFailures,
                contrastFailures: result.contrastFailures,
                layoutFailures: result.layoutFailures,
              },
            })
          }
          screenshots.push(pageScreenshot)
          if (name === 'fingerprint' || name === 'settings') {
            await targetPage.bringToFront()
            await setFrontWindowSize(
              size.width, size.height, session.process.child.pid)
            await targetPage.waitForTimeout(300)
            const nativeScreenshot = path.join(
              dirs.native, `full-${theme}-${name}-${size.width}x${size.height}.png`)
            await captureNativeScreenshot(nativeScreenshot, session.process.child.pid)
            const dimensions = await pngDimensions(nativeScreenshot)
            if (dimensions.width !== size.width || dimensions.height !== size.height) {
              throw Object.assign(
                new Error(
                  `${theme} ${name} native screenshot dimensions did not match ${size.width}x${size.height}`),
                {details: {actual: dimensions, expected: size}},
              )
            }
            screenshots.push(nativeScreenshot)
          }
          if (name === 'fingerprint') {
            for (const visualState of ['fail', 'unavailable']) {
              await targetPage.evaluate(visualState => {
                const status = document.querySelector('#status')
                const row = document.querySelector('.row-status')
                status.dataset.state = 'fail'
                if (visualState === 'fail') {
                  status.textContent = '8 of 9 values match this profile\'s Persona.'
                  if (row) {
                    row.dataset.state = 'fail'
                    row.textContent = 'Different'
                  }
                } else {
                  status.textContent = 'Could not load fingerprint data: Persona is unavailable.'
                  if (row) {
                    row.dataset.state = 'na'
                    row.textContent = 'Unavailable'
                  }
                }
              }, visualState)
              const stateScreenshot = path.join(
                dirs.page,
                `full-${theme}-fingerprint-${visualState}-${size.width}x${size.height}.png`)
              await targetPage.screenshot({path: stateScreenshot, fullPage: false})
              await assertCurrentWebUi(
                targetPage,
                `${theme} fingerprint ${visualState} ${size.width}x${size.height}`)
              screenshots.push(stateScreenshot)
            }
          }
          if (targetPage !== page) {
            await targetPage.close()
          }
        }
      }
    } finally {
      await session.close()
    }
  }
  return {screenshots}
}

export async function runFull({config, dirs, probe, report, runId}) {
  await runScenario(report, 'full-profile-lifecycle', async () =>
    await runProfileLifecycle({config, dirs, probe, runId}))

  let proxyFixtures
  await runScenario(report, 'full-proxy-fixtures', async () => {
    proxyFixtures = await loadProxyFixtures(config.proxyFixtures)
    if (proxyFixtures.status === 'BLOCKED') {
      return proxyFixtures
    }
    return {
      fixtures: [
        publicProxyRecord(proxyFixtures.http),
        publicProxyRecord(proxyFixtures.socks5),
      ],
    }
  })
  if (proxyFixtures?.status === 'PASS') {
    for (const scheme of ['http', 'socks5']) {
      await runScenario(report, `full-proxy-${scheme}`, async () =>
        await verifyProxy({
          config,
          dirs,
          fixture: proxyFixtures[scheme],
          probe,
          runId,
        }))
    }
    await runScenario(report, 'full-proxy-switch-persistence', async () =>
      await verifyProxySwitchPersistence({
        config,
        dirs,
        fixtures: proxyFixtures,
        probe,
        runId,
      }))
  }

  await runScenario(report, 'full-local-mv3-extension', async () =>
    await runLocalExtensionLifecycle({
      config,
      dirs,
      fixtureDir: path.join(config.braveRoot, 'tools', 'fingerprint_browser', 'qa', 'fixtures', 'mv3'),
      probe,
      runId,
    }))

  const primaryUrl = process.env.FP_QA_PRIMARY_EXTENSION_URL
  await runScenario(report, 'full-cws-primary-extension', async () => {
    if (!primaryUrl) {
      return {
        reason: 'FP_QA_PRIMARY_EXTENSION_URL is required for the original crash extension',
        status: 'BLOCKED',
      }
    }
    return await runWebStoreExtensionLifecycle({
      config,
      dirs,
      extensionUrl: primaryUrl,
      label: 'primary',
      runId,
    })
  })
  await runScenario(report, 'full-cws-google-translate', async () =>
    await runWebStoreExtensionLifecycle({
      config,
      dirs,
      extensionUrl: GOOGLE_TRANSLATE_URL,
      label: 'google-translate',
      runId,
    }))

  await runScenario(report, 'full-third-party-fingerprint-evidence', async () =>
    await runThirdPartyScans({config, dirs, runId}))

  await runScenario(report, 'full-tls-source-regression', async () =>
    await verifyTlsSourceScope(config))

  await runScenario(report, 'full-ui-theme-size-matrix', async () =>
    await runUiMatrix({config, dirs, probe, runId}))

  await runScenario(report, 'full-native-toolbar-menu-evidence', async () =>
    await importNativeEvidence(dirs.native, report.artifacts))

}
