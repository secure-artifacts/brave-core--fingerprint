// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.
import path from 'node:path'

import {
  auditCurrentPage,
  navigateAndCapture,
  startQaSession,
} from '../lib/browser.mjs'
import {
  runLocalExtensionLifecycle,
  runWebStoreExtensionLifecycle,
} from '../lib/extensions.mjs'
import { loadProxyFixtures, publicProxyRecord } from '../lib/fixtures.mjs'
import {
  applyVerifiedProfileProxy,
  collectWebRtcCandidates,
  readProfileProxyState,
  renderProxyValidationError,
  runProfileLifecycle,
  setProfileProxy,
  verifyProfileProxy,
  waitForProfileProxyIdle,
} from '../lib/profile.mjs'
import { runScenario } from '../lib/report.mjs'
import {
  captureNativeScreenshot,
  clickNativeText,
  clickNativeWindowOffset,
  nativeScreenshotHasText,
  pngDimensions,
  run,
  setFrontWindowSize,
} from '../lib/system.mjs'
import { importNativeEvidence } from '../lib/native_evidence.mjs'

const GOOGLE_TRANSLATE_URL =
  'https://chromewebstore.google.com/detail/google-translate/aapbdbdomjkkjkaonfhkkikfgjllcleb'
const VERIFICATION_BUSY_ERROR = '另一个代理验证任务正在运行。'

async function verifyProfileProxyAfterRevalidation(page, draft) {
  let result
  for (let attempt = 0; attempt < 3; attempt += 1) {
    result = await verifyProfileProxy(page, draft)
    if (result.actionError !== VERIFICATION_BUSY_ERROR) {
      return result
    }
    await waitForProfileProxyIdle(page)
  }
  return result
}

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
  const timezone =
    typeof parsed.timezone === 'string' ? parsed.timezone : parsed.timezone?.id
  if (!countryCode || !timezone) {
    throw new Error(
      'Geo verification response requires countryCode/country_code and timezone',
    )
  }
  return { countryCode: String(countryCode).toUpperCase(), timezone }
}

async function collectProxySurfaceProbe(page) {
  const origin = 'https://example.com'
  await page.context().grantPermissions(['geolocation'], { origin })
  const response = await page.goto(`${origin}/`, {
    timeout: 60000,
    waitUntil: 'domcontentloaded',
  })
  if (!response?.ok()) {
    throw new Error(`Public proxy probe returned ${response?.status()}`)
  }
  return await page.evaluate(async () => {
    const geolocation = await new Promise((resolve, reject) => {
      navigator.geolocation.getCurrentPosition(
        (position) =>
          resolve({
            accuracy: position.coords.accuracy,
            latitude: position.coords.latitude,
            longitude: position.coords.longitude,
          }),
        (error) => reject(new Error(`Geolocation failed: ${error.message}`)),
        { timeout: 10000 },
      )
    })
    return {
      basic: { languages: [...navigator.languages] },
      geolocation,
      timezone: Intl.DateTimeFormat().resolvedOptions().timeZone,
    }
  })
}

async function assertCurrentWebUi(page, label) {
  const audit = await auditCurrentPage(page)
  if (
    audit.componentContrastFailures.length > 0
    || audit.contrastFailures.length > 0
    || audit.layoutFailures.length > 0
  ) {
    throw Object.assign(new Error(`${label} failed WebUI checks`), {
      details: audit,
    })
  }
  return audit
}

async function profilePrefs(page, updates = null) {
  await page.goto('brave://settings/fingerprintProfileProxy', {
    waitUntil: 'domcontentloaded',
  })
  return await page.evaluate(async (updates) => {
    if (updates) {
      if (updates['intl.accept_languages']) {
        await chrome.settingsPrivate.setPref(
          'intl.selected_languages',
          updates['intl.accept_languages'],
        )
      }
      for (const [key, value] of Object.entries(updates)) {
        await chrome.settingsPrivate.setPref(key, value)
      }
    }
    const result = {}
    for (const key of [
      'intl.accept_languages',
      'intl.selected_languages',
      'webrtc.ip_handling_policy',
    ]) {
      result[key] = (await chrome.settingsPrivate.getPref(key)).value
    }
    return result
  }, updates)
}

async function verifyProxy({ config, dirs, fixture, probe, runId }) {
  const profilePath = `/tmp/fingerprint-browser-${runId}/proxy-${fixture.scheme}`
  const events = []
  let session = await startQaSession({
    app: config.app,
    language: null,
    logDir: dirs.logs,
    name: `proxy-${fixture.scheme}`,
    profilePath,
  })
  events.push(session.events)
  try {
    let page = session.context.pages()[0] || (await session.context.newPage())
    const originalPrefs = await profilePrefs(page, {
      'intl.accept_languages': 'fr-FR,fr',
      'webrtc.ip_handling_policy': 'default_public_and_private_interfaces',
    })
    if (
      originalPrefs['intl.accept_languages'] !== 'fr-FR,fr'
      || originalPrefs['webrtc.ip_handling_policy']
        !== 'default_public_and_private_interfaces'
    ) {
      throw Object.assign(
        new Error('Could not establish restoration sentinels'),
        {
          details: originalPrefs,
        },
      )
    }
    const defaultState = await readProfileProxyState(page)
    if (defaultState.enabled) {
      throw new Error(
        `${fixture.scheme} proxy was unexpectedly enabled by default`,
      )
    }
    const defaultScreenshot = path.join(
      dirs.page,
      `full-proxy-${fixture.scheme}-default.png`,
    )
    await page.screenshot({ path: defaultScreenshot, fullPage: false })
    await assertCurrentWebUi(page, `${fixture.scheme} proxy default state`)
    const invalidScreenshot = path.join(
      dirs.page,
      `full-proxy-${fixture.scheme}-validation-error.png`,
    )
    const validation = await renderProxyValidationError(page, invalidScreenshot)
    await assertCurrentWebUi(page, `${fixture.scheme} proxy validation`)

    const verified = await verifyProfileProxy(page, {
      ...fixture,
      enabled: true,
    })
    if (
      !verified.verification?.success
      || verified.actionError
      || verified.verification.egressIp !== fixture.expectedIp
      || verified.verification.geo?.countryCode !== fixture.countryCode
      || verified.verification.geo?.timezone !== fixture.timezone
    ) {
      throw Object.assign(
        new Error(`Could not verify ${fixture.scheme} proxy`),
        { details: { expected: publicProxyRecord(fixture), verified } },
      )
    }
    const verifiedScreenshot = path.join(
      dirs.page,
      `full-proxy-${fixture.scheme}-verified.png`,
    )
    await page.screenshot({ path: verifiedScreenshot, fullPage: false })
    await assertCurrentWebUi(page, `${fixture.scheme} proxy verified state`)

    const applied = await applyVerifiedProfileProxy(page)
    if (
      !applied.enabled
      || applied.state !== 'active'
      || applied.egressIp !== fixture.expectedIp
      || applied.activeGeo?.countryCode !== fixture.countryCode
      || applied.activeGeo?.timezone !== fixture.timezone
    ) {
      throw Object.assign(
        new Error(`Could not apply ${fixture.scheme} proxy`),
        { details: applied },
      )
    }
    const activeScreenshot = path.join(
      dirs.page,
      `full-proxy-${fixture.scheme}-active.png`,
    )
    await page.screenshot({ path: activeScreenshot, fullPage: false })
    await assertCurrentWebUi(page, `${fixture.scheme} proxy active state`)

    const toolbarProbe = await page.goto('https://example.com/', {
      timeout: 60000,
      waitUntil: 'domcontentloaded',
    })
    if (!toolbarProbe?.ok()) {
      throw new Error(`${fixture.scheme} active toolbar probe failed`)
    }
    await page.bringToFront()
    await page.waitForTimeout(500)
    const activeToolbarScreenshot = path.join(
      dirs.native,
      `full-proxy-${fixture.scheme}-active-toolbar.png`,
    )
    await captureNativeScreenshot(
      activeToolbarScreenshot,
      session.process.child.pid,
    )

    const response = await page.goto(fixture.verifyUrl, {
      timeout: 60000,
      waitUntil: 'domcontentloaded',
    })
    if (!response?.ok()) {
      throw new Error(
        `${fixture.scheme} proxy verification returned ${response?.status()}`,
      )
    }
    const body = await page.locator('body').innerText()
    const observedIp = observedIpFromBody(body)
    if (observedIp !== fixture.expectedIp) {
      throw new Error(
        `${fixture.scheme} exit IP mismatch: ${observedIp} != ${fixture.expectedIp}`,
      )
    }
    const geoResponse = await page.goto(fixture.geoVerifyUrl, {
      timeout: 60000,
      waitUntil: 'domcontentloaded',
    })
    if (!geoResponse?.ok()) {
      throw new Error(
        `${fixture.scheme} Geo verification returned ${geoResponse?.status()}`,
      )
    }
    const networkGeo = observedGeoFromBody(
      await page.locator('body').innerText(),
    )
    if (
      networkGeo.countryCode !== fixture.countryCode
      || networkGeo.timezone !== fixture.timezone
    ) {
      throw Object.assign(new Error(`${fixture.scheme} exit Geo mismatch`), {
        details: { expected: publicProxyRecord(fixture), networkGeo },
      })
    }

    for (const url of [
      'https://www.google.com/',
      'https://www.facebook.com/',
    ]) {
      const siteResponse = await page.goto(url, {
        timeout: 60000,
        waitUntil: 'domcontentloaded',
      })
      if (!siteResponse?.ok()) {
        throw new Error(
          `${fixture.scheme} proxy navigation returned ${siteResponse?.status()} for ${url}`,
        )
      }
    }
    const observed = await collectProxySurfaceProbe(page)
    if (observed.timezone !== fixture.timezone) {
      throw new Error(
        `${fixture.scheme} timezone mismatch: ${observed.timezone} != ${fixture.timezone}`,
      )
    }
    const languages = observed.basic.languages.map(normalizeLanguage)
    if (
      !languages.some((language) =>
        language.startsWith(normalizeLanguage(fixture.language)),
      )
    ) {
      throw new Error(
        `${fixture.scheme} language mismatch: ${languages.join(', ')}`,
      )
    }
    if (
      !Number.isFinite(observed.geolocation.latitude)
      || !Number.isFinite(observed.geolocation.longitude)
    ) {
      throw new Error(`${fixture.scheme} geolocation was unavailable`)
    }
    const latitudeDelta = Math.abs(
      observed.geolocation.latitude - fixture.latitude,
    )
    const longitudeDelta = Math.abs(
      observed.geolocation.longitude - fixture.longitude,
    )
    if (latitudeDelta > 0.1 || longitudeDelta > 0.1) {
      throw new Error(`${fixture.scheme} geolocation mismatch`)
    }

    const candidates = await collectWebRtcCandidates(page)
    const leakingCandidates = candidates.filter((candidate) => {
      const srflx = candidate.includes(' typ srflx ')
      return srflx && !candidate.includes(fixture.expectedIp)
    })
    if (leakingCandidates.length > 0) {
      throw Object.assign(
        new Error(`${fixture.scheme} WebRTC leaked a non-proxy srflx address`),
        {
          details: { candidates, leakingCandidates },
        },
      )
    }

    await session.close()
    session = await startQaSession({
      app: config.app,
      language: null,
      logDir: dirs.logs,
      name: `proxy-${fixture.scheme}-restart`,
      profilePath,
    })
    events.push(session.events)
    page = session.context.pages()[0] || (await session.context.newPage())
    const persisted = await readProfileProxyState(page)
    if (
      !persisted.enabled
      || persisted.scheme !== fixture.scheme
      || persisted.host !== fixture.host
      || persisted.port !== fixture.port
    ) {
      throw Object.assign(
        new Error(`${fixture.scheme} proxy did not persist across restart`),
        {
          details: persisted,
        },
      )
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

    const authAttempt = await verifyProfileProxyAfterRevalidation(page, {
      ...fixture,
      enabled: true,
      password: `${fixture.password}-intentionally-wrong`,
    })
    if (authAttempt.verification || !authAttempt.actionError) {
      throw Object.assign(
        new Error(`${fixture.scheme} wrong credentials were not rejected`),
        { details: authAttempt },
      )
    }
    const authState = await readProfileProxyState(page, false)
    if (
      !authState.enabled
      || !['active', 'stale'].includes(authState.state)
      || authState.egressIp !== fixture.expectedIp
    ) {
      throw Object.assign(
        new Error(`${fixture.scheme} failed draft replaced active proxy`),
        { details: authState },
      )
    }
    const authScreenshot = path.join(
      dirs.page,
      `full-proxy-${fixture.scheme}-authentication-error.png`,
    )
    await page.evaluate(() => {
      function find(root) {
        const direct = root.querySelector?.(
          'settings-fingerprint-profile-proxy-subpage',
        )
        if (direct) return direct
        for (const element of root.querySelectorAll?.('*') || []) {
          if (element.shadowRoot) {
            const nested = find(element.shadowRoot)
            if (nested) return nested
          }
        }
        return null
      }
      find(document)
        ?.shadowRoot?.querySelector('#actionError')
        ?.scrollIntoView({ block: 'center' })
    })
    await page.screenshot({ path: authScreenshot, fullPage: false })
    await assertCurrentWebUi(
      page,
      `${fixture.scheme} proxy authentication error`,
    )

    await setProfileProxy(page, { enabled: false })
    const direct = await page.goto(probe.origin, {
      waitUntil: 'domcontentloaded',
    })
    if (!direct?.ok()) {
      throw new Error('Direct settings were not restored after disabling proxy')
    }
    const restoredPrefs = await profilePrefs(page)
    if (JSON.stringify(restoredPrefs) !== JSON.stringify(originalPrefs)) {
      throw Object.assign(
        new Error('Language or WebRTC preference was not restored'),
        {
          details: { originalPrefs, restoredPrefs },
        },
      )
    }
    const failures = events.flatMap((eventSet) => [
      ...eventSet.browserExits.map(
        (event) => `browser exited ${event.code ?? event.signal}`,
      ),
      ...eventSet.crashes.map((event) => `renderer crash ${event.page}`),
      ...eventSet.pageErrors.map((event) => `pageerror ${event.message}`),
      ...eventSet.disconnected.map(() => 'CDP disconnected'),
    ])
    if (failures.length > 0) {
      throw Object.assign(new Error(failures.join('; ')), { details: events })
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
        verifiedScreenshot,
        activeScreenshot,
        activeToolbarScreenshot,
        authScreenshot,
      ],
      validation,
    }
  } finally {
    await session.close()
  }
}

function thirdPartyLieSignals(bodyText) {
  const signals = []
  const patterns = [
    /\b(?:lies|mismatches?)\s*[(:=]\s*([1-9]\d*)\b/gi,
    /\b(?:lied|mismatched?)\s*[:=]\s*(true|yes|detected)\b/gi,
  ]
  for (const pattern of patterns) {
    for (const match of bodyText.matchAll(pattern)) {
      signals.push(match[0].replaceAll(/\s+/g, ' ').trim())
    }
  }
  return [...new Set(signals)]
}

export function analyzeThirdPartyScan({
  bodyText,
  expectedIp,
  expectedLanguage,
  expectedTimezone,
  languages,
  name,
  responseStatus,
  timezone,
  webRtcCandidates,
}) {
  const failures = []
  const lieSignals = thirdPartyLieSignals(bodyText)
  if (
    !Number.isInteger(responseStatus)
    || responseStatus < 200
    || responseStatus >= 400
  ) {
    failures.push(`HTTP status ${responseStatus ?? 'unavailable'}`)
  }
  if (timezone !== expectedTimezone) {
    failures.push(
      `timezone ${timezone || 'unavailable'} != ${expectedTimezone}`,
    )
  }
  const normalizedLanguages = languages.map(normalizeLanguage)
  if (
    !normalizedLanguages.some((language) =>
      language.startsWith(normalizeLanguage(expectedLanguage)),
    )
  ) {
    failures.push(
      `language ${normalizedLanguages.join(', ') || 'unavailable'} != ${expectedLanguage}`,
    )
  }
  if (name === 'browserleaks' && !bodyText.includes(expectedIp)) {
    failures.push(`BrowserLeaks did not report expected exit IP ${expectedIp}`)
  }
  if (lieSignals.length > 0) {
    failures.push(`explicit lie/mismatch signals: ${lieSignals.join(', ')}`)
  }
  const leakingCandidates = webRtcCandidates.filter(
    (candidate) =>
      candidate.includes(' typ srflx ') && !candidate.includes(expectedIp),
  )
  if (leakingCandidates.length > 0) {
    failures.push('WebRTC exposed a non-proxy srflx address')
  }
  return {
    failures,
    language: normalizedLanguages,
    lieSignals,
    pass: failures.length === 0,
    timezone,
    webRtcCandidates,
    webRtcLeaks: leakingCandidates,
  }
}

async function runThirdPartyScans({ config, dirs, fixture, runId }) {
  const session = await startQaSession({
    app: config.app,
    logDir: dirs.logs,
    name: 'fingerprint-scans',
    profilePath: `/tmp/fingerprint-browser-${runId}/scans`,
  })
  const scans = [
    ['creepjs', 'https://abrahamjuliot.github.io/creepjs/'],
    ['fingerprintjs', 'https://fingerprint.com/demo/'],
    ['browserleaks', 'https://browserleaks.com/ip'],
    ['browserleaks-ssl', 'https://browserleaks.com/ssl'],
  ]
  try {
    const page = session.context.pages()[0] || (await session.context.newPage())
    let observedIp = null
    if (fixture) {
      const applied = await setProfileProxy(page, fixture)
      if (
        !applied.enabled
        || applied.state !== 'active'
        || applied.egressIp !== fixture.expectedIp
      ) {
        return {
          reason: 'HTTP proxy could not be applied for third-party scans',
          status: 'FAIL',
        }
      }
      const verificationPage = await session.context.newPage()
      const ipResponse = await verificationPage.goto(fixture.verifyUrl, {
        timeout: 60000,
        waitUntil: 'domcontentloaded',
      })
      observedIp = observedIpFromBody(
        await verificationPage.locator('body').innerText(),
      )
      await verificationPage.close()
      if (!ipResponse?.ok() || observedIp !== fixture.expectedIp) {
        return {
          observedIp,
          reason: `Third-party scan exit IP mismatch: ${observedIp} != ${fixture.expectedIp}`,
          status: 'FAIL',
        }
      }
    }
    const evidence = []
    for (const [name, url] of scans) {
      const screenshot = path.join(dirs.page, `full-scan-${name}.png`)
      try {
        const response = await page.goto(url, {
          timeout: 60000,
          waitUntil: 'domcontentloaded',
        })
        await page.waitForTimeout(5000)
        const surface = await page.evaluate(() => ({
          bodyText: document.body?.innerText || '',
          languages: [...navigator.languages],
          timezone: Intl.DateTimeFormat().resolvedOptions().timeZone,
        }))
        const webRtcCandidates = await collectWebRtcCandidates(page)
        await page.screenshot({ path: screenshot, fullPage: false })
        const analysis = fixture
          ? analyzeThirdPartyScan({
              ...surface,
              expectedIp: fixture.expectedIp,
              expectedLanguage: fixture.language,
              expectedTimezone: fixture.timezone,
              name,
              responseStatus: response?.status() ?? null,
              webRtcCandidates,
            })
          : {
              failures: [
                'HTTP proxy fixture is required for machine assertions',
              ],
              pass: false,
              ...surface,
              webRtcCandidates,
            }
        evidence.push({
          analysis,
          name,
          screenshot,
          status: response?.status() ?? null,
          url: page.url(),
        })
      } catch (error) {
        evidence.push({
          analysis: { failures: [error.message], pass: false },
          name,
          screenshot,
          status: null,
          url: page.url(),
        })
      }
    }
    const failed = evidence.filter((item) => !item.analysis.pass)
    return {
      evidence,
      observedIp,
      reason:
        failed.length > 0
          ? `${failed.length} third-party fingerprint scans failed machine assertions`
          : undefined,
      screenshots: evidence.map((item) => item.screenshot),
      status: fixture ? (failed.length > 0 ? 'FAIL' : 'PASS') : 'BLOCKED',
    }
  } finally {
    await session.close()
  }
}

async function verifyTlsSourceScope(config) {
  const mergeBase = await run(
    'git',
    ['merge-base', 'HEAD', 'upstream/master'],
    { check: true, cwd: config.braveRoot },
  )
  const base = mergeBase.stdout.trim()
  const diff = await run(
    'git',
    [
      'diff',
      '--name-only',
      base,
      '--',
      'chromium_src/net/socket/ssl_client_socket_impl.cc',
      'chromium_src/third_party/boringssl',
      'patches/*boringssl*',
      'patches/*ssl_client_socket*',
    ],
    { check: true, cwd: config.braveRoot },
  )
  const changed = diff.stdout.trim().split('\n').filter(Boolean)
  if (changed.length > 0) {
    throw Object.assign(
      new Error('TLS implementation differs from upstream base'),
      {
        details: { base, changed },
      },
    )
  }
  return { base, changed }
}

export async function runUiMatrix({ config, dirs, probe, runId }) {
  const sizes = [
    { height: 768, width: 1024 },
    { height: 800, width: 1280 },
    { height: 982, width: 1512 },
  ]
  const states = [
    ['new-tab', 'brave://newtab/'],
    ['google', 'https://www.google.com/'],
    ['facebook', 'https://www.facebook.com/'],
    ['settings', 'brave://settings/'],
    ['proxy', 'brave://settings/fingerprintProfileProxy'],
    ['fingerprint', 'brave://fingerprint-test/'],
    ['guide', 'brave://fingerprint-guide/'],
    ['diagnostics', 'brave://diagnostics/'],
    ['crashes', 'brave://crashes/'],
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
        brave: { dark_mode_migrated: true },
        browser: { theme: { color_scheme2: theme === 'dark' ? 2 : 1 } },
      },
    })
    try {
      const page =
        session.context.pages()[0] || (await session.context.newPage())
      await page.emulateMedia({ colorScheme: theme })
      for (const size of sizes) {
        await setFrontWindowSize(
          size.width,
          size.height,
          session.process.child.pid,
        )
        await page.setViewportSize(size)
        for (const [name, url] of states) {
          let targetPage = page
          if (name === 'fingerprint') {
            await page.goto(probe.origin, { waitUntil: 'domcontentloaded' })
            targetPage = await session.context.newPage()
            await targetPage.setViewportSize(size)
            await targetPage.emulateMedia({ colorScheme: theme })
          }
          const pageScreenshot = path.join(
            dirs.page,
            `full-${theme}-${name}-${size.width}x${size.height}.png`,
          )
          const result = await navigateAndCapture({
            colorScheme: theme,
            page: targetPage,
            screenshot: pageScreenshot,
            url,
            validateContrast: [
              'crashes',
              'diagnostics',
              'fingerprint',
              'guide',
              'proxy',
              'settings',
            ].includes(name),
            validateLayout: [
              'crashes',
              'diagnostics',
              'fingerprint',
              'guide',
              'proxy',
              'settings',
            ].includes(name),
          })
          if (
            result.componentContrastFailures.length > 0
            || result.contrastFailures.length > 0
            || result.layoutFailures.length > 0
          ) {
            throw Object.assign(
              new Error(
                `${theme} ${name} ${size.width}x${size.height} failed UI checks`,
              ),
              {
                details: {
                  componentContrastFailures: result.componentContrastFailures,
                  contrastFailures: result.contrastFailures,
                  layoutFailures: result.layoutFailures,
                },
              },
            )
          }
          screenshots.push(pageScreenshot)
          if (
            name === 'fingerprint'
            || name === 'guide'
            || name === 'diagnostics'
            || name === 'crashes'
            || name === 'settings'
            || name === 'proxy'
          ) {
            await targetPage.bringToFront()
            await setFrontWindowSize(
              size.width,
              size.height,
              session.process.child.pid,
            )
            await targetPage.waitForTimeout(300)
            const nativeScreenshot = path.join(
              dirs.native,
              `full-${theme}-${name}-${size.width}x${size.height}.png`,
            )
            await captureNativeScreenshot(
              nativeScreenshot,
              session.process.child.pid,
            )
            const dimensions = await pngDimensions(nativeScreenshot)
            if (
              dimensions.width !== size.width
              || dimensions.height !== size.height
            ) {
              throw Object.assign(
                new Error(
                  `${theme} ${name} native screenshot dimensions did not match ${size.width}x${size.height}`,
                ),
                { details: { actual: dimensions, expected: size } },
              )
            }
            screenshots.push(nativeScreenshot)
          }
          if (name === 'fingerprint') {
            for (const visualState of ['fail', 'unavailable']) {
              await targetPage.evaluate((visualState) => {
                const status = document.querySelector('#status')
                const row = document.querySelector('.row-status')
                status.dataset.state = 'fail'
                if (visualState === 'fail') {
                  status.textContent = '9 项中有 8 项与当前浏览器身份匹配。'
                  if (row) {
                    row.dataset.state = 'fail'
                    row.textContent = '不同'
                  }
                } else {
                  status.textContent = '无法加载指纹数据：浏览器身份不可用。'
                  if (row) {
                    row.dataset.state = 'na'
                    row.textContent = '不可用'
                  }
                }
              }, visualState)
              const stateScreenshot = path.join(
                dirs.page,
                `full-${theme}-fingerprint-${visualState}-${size.width}x${size.height}.png`,
              )
              await targetPage.screenshot({
                path: stateScreenshot,
                fullPage: false,
              })
              await assertCurrentWebUi(
                targetPage,
                `${theme} fingerprint ${visualState} ${size.width}x${size.height}`,
              )
              screenshots.push(stateScreenshot)
            }
          }
          if (name === 'proxy') {
            const validationScreenshot = path.join(
              dirs.page,
              `full-${theme}-proxy-validation-${size.width}x${size.height}.png`,
            )
            await renderProxyValidationError(targetPage, validationScreenshot)
            await assertCurrentWebUi(
              targetPage,
              `${theme} proxy validation ${size.width}x${size.height}`,
            )
            screenshots.push(validationScreenshot)
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
  return { screenshots }
}

export async function runProxyToolbarFlow({ config, dirs, runId }) {
  const session = await startQaSession({
    app: config.app,
    logDir: dirs.logs,
    name: 'proxy-toolbar',
    profilePath: `/tmp/fingerprint-browser-${runId}/proxy-toolbar`,
    profilePreferences: {
      brave: { dark_mode_migrated: true },
      browser: { theme: { color_scheme2: 1 } },
    },
  })
  try {
    const page = session.context.pages()[0] || (await session.context.newPage())
    await page.goto('https://example.com/', { waitUntil: 'domcontentloaded' })
    await page.bringToFront()
    await page.waitForTimeout(500)

    const toolbarScreenshot = path.join(
      dirs.native,
      'full-proxy-toolbar-button.png',
    )
    await captureNativeScreenshot(toolbarScreenshot, session.process.child.pid)
    await clickNativeText(
      toolbarScreenshot,
      '用户配置文件代理',
      session.process.child.pid,
    ).catch(
      async () =>
        await clickNativeWindowOffset(50, 60, session.process.child.pid),
    )
    await page.waitForTimeout(500)

    const bubbleScreenshot = path.join(
      dirs.native,
      'full-proxy-toolbar-bubble.png',
    )
    await captureNativeScreenshot(bubbleScreenshot, session.process.child.pid)
    const bubbleIsVisible = async () =>
      (await nativeScreenshotHasText(bubbleScreenshot, '用户配置文件代理'))
      && (await nativeScreenshotHasText(bubbleScreenshot, '配置'))
    if (!(await bubbleIsVisible())) {
      await clickNativeWindowOffset(50, 60, session.process.child.pid)
      await page.waitForTimeout(500)
      await captureNativeScreenshot(bubbleScreenshot, session.process.child.pid)
    }
    if (!(await bubbleIsVisible())) {
      throw new Error('Profile proxy toolbar bubble was not visible')
    }
    await clickNativeText(bubbleScreenshot, '配置', session.process.child.pid)
    await page.waitForTimeout(700)

    const afterConfigureScreenshot = path.join(
      dirs.native,
      'full-proxy-toolbar-after-configure.png',
    )
    await captureNativeScreenshot(
      afterConfigureScreenshot,
      session.process.child.pid,
    )
    if (await nativeScreenshotHasText(afterConfigureScreenshot, '配置')) {
      await clickNativeWindowOffset(270, 282, session.process.child.pid)
    }

    let settingsPage
    for (let attempt = 0; attempt < 100; attempt += 1) {
      settingsPage = session.context
        .pages()
        .find((candidate) =>
          /^(?:brave|chrome):\/\/settings\/fingerprintProfileProxy/.test(
            candidate.url(),
          ),
        )
      if (settingsPage) break
      await page.waitForTimeout(100)
    }
    if (!settingsPage) {
      throw Object.assign(
        new Error('Profile proxy toolbar did not open its settings route'),
        {
          details: {
            urls: session.context.pages().map((candidate) => candidate.url()),
          },
        },
      )
    }
    await settingsPage.bringToFront()
    await settingsPage.waitForFunction(
      () => {
        function find(root) {
          const direct = root.querySelector?.(
            'settings-fingerprint-profile-proxy-subpage',
          )
          if (direct) return direct
          for (const element of root.querySelectorAll?.('*') || []) {
            if (element.shadowRoot) {
              const nested = find(element.shadowRoot)
              if (nested) return nested
            }
          }
          return null
        }
        const proxyPage = find(document)
        return Boolean(
          proxyPage?.getClientRects().length
            && proxyPage.shadowRoot?.querySelector('#host'),
        )
      },
      null,
      { timeout: 15000 },
    )
    await settingsPage.waitForTimeout(1500)

    const pageScreenshot = path.join(
      dirs.page,
      'full-proxy-toolbar-settings-route.png',
    )
    await settingsPage.screenshot({ path: pageScreenshot, fullPage: false })
    await assertCurrentWebUi(
      settingsPage,
      'Profile proxy toolbar settings route',
    )
    return {
      screenshots: [
        toolbarScreenshot,
        bubbleScreenshot,
        afterConfigureScreenshot,
        pageScreenshot,
      ],
      url: settingsPage.url(),
    }
  } finally {
    await session.close()
  }
}

export async function runProxyFixtures({ config, dirs, probe, report, runId }) {
  let proxyFixtures
  await runScenario(report, 'full-proxy-fixtures', async () => {
    proxyFixtures = await loadProxyFixtures(config.proxyFixtures)
    if (proxyFixtures.status === 'BLOCKED') {
      return {
        fixtures: proxyFixtures.http
          ? [publicProxyRecord(proxyFixtures.http)]
          : [],
        reason: proxyFixtures.reason,
        status: 'BLOCKED',
      }
    }
    return {
      fixtures: [publicProxyRecord(proxyFixtures.http)],
    }
  })
  if (proxyFixtures?.http) {
    await runScenario(
      report,
      'full-proxy-http',
      async () =>
        await verifyProxy({
          config,
          dirs,
          fixture: proxyFixtures.http,
          probe,
          runId,
        }),
    )
  }
  return proxyFixtures
}

export async function runFull({ config, dirs, probe, report, runId }) {
  await runScenario(
    report,
    'full-profile-lifecycle',
    async () => await runProfileLifecycle({ config, dirs, probe, runId }),
  )

  const proxyFixtures = await runProxyFixtures({
    config,
    dirs,
    probe,
    report,
    runId,
  })

  await runScenario(
    report,
    'full-local-mv3-extension',
    async () =>
      await runLocalExtensionLifecycle({
        config,
        dirs,
        fixtureDir: path.join(
          config.braveRoot,
          'tools',
          'fingerprint_browser',
          'qa',
          'fixtures',
          'mv3',
        ),
        probe,
        runId,
      }),
  )

  const primaryUrl = process.env.FP_QA_PRIMARY_EXTENSION_URL
  if (primaryUrl) {
    await runScenario(
      report,
      'full-cws-primary-extension',
      async () =>
        await runWebStoreExtensionLifecycle({
          config,
          dirs,
          extensionUrl: primaryUrl,
          label: 'primary',
          runId,
        }),
    )
  }
  await runScenario(
    report,
    'full-cws-google-translate',
    async () =>
      await runWebStoreExtensionLifecycle({
        config,
        dirs,
        extensionUrl: GOOGLE_TRANSLATE_URL,
        label: 'google-translate',
        runId,
      }),
  )

  await runScenario(
    report,
    'full-third-party-fingerprint-evidence',
    async () =>
      await runThirdPartyScans({
        config,
        dirs,
        fixture: proxyFixtures?.http || null,
        runId,
      }),
  )

  await runScenario(
    report,
    'full-tls-source-regression',
    async () => await verifyTlsSourceScope(config),
  )

  await runScenario(
    report,
    'full-proxy-toolbar-flow',
    async () => await runProxyToolbarFlow({ config, dirs, runId }),
  )

  await runScenario(
    report,
    'full-ui-theme-size-matrix',
    async () => await runUiMatrix({ config, dirs, probe, runId }),
  )

  await runScenario(
    report,
    'full-native-toolbar-menu-evidence',
    async () => await importNativeEvidence(dirs.native, report.artifacts),
  )
}
