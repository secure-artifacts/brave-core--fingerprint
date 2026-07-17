import fs from 'node:fs/promises'
import path from 'node:path'

import {startQaSession} from './browser.mjs'

async function findProxyElement(page) {
  await page.waitForFunction(() => {
    function find(root) {
      const direct = root.querySelector?.('settings-fingerprint-profile-proxy-subpage')
      if (direct) return direct
      for (const element of root.querySelectorAll?.('*') || []) {
        if (element.shadowRoot) {
          const nested = find(element.shadowRoot)
          if (nested) return nested
        }
      }
      return null
    }
    return Boolean(find(document))
  }, null, {timeout: 15000})
}

export async function readProfileProxyState(page, navigate = true) {
  if (navigate) {
    await page.goto('brave://settings/privacy', {waitUntil: 'domcontentloaded'})
    await findProxyElement(page)
    await page.waitForTimeout(500)
  }
  return await page.evaluate(() => {
    function find(root) {
      const direct = root.querySelector?.('settings-fingerprint-profile-proxy-subpage')
      if (direct) return direct
      for (const element of root.querySelectorAll?.('*') || []) {
        if (element.shadowRoot) {
          const nested = find(element.shadowRoot)
          if (nested) return nested
        }
      }
      return null
    }
    const element = find(document)
    return {
      conflictWarning: element.conflictWarning_,
      enabled: element.enabledPref_.value,
      geoWarning: element.geoWarning_,
      host: element.host_,
      lastError: element.lastError_,
      port: Number(element.port_),
      savedStatus: element.savedStatus_,
      scheme: element.scheme_,
    }
  })
}

export async function waitForProfileProxyError(page, timeoutMs = 15000) {
  await page.goto('brave://settings/privacy', {waitUntil: 'domcontentloaded'})
  await findProxyElement(page)
  await page.waitForFunction(() => {
    function find(root) {
      const direct = root.querySelector?.('settings-fingerprint-profile-proxy-subpage')
      if (direct) return direct
      for (const element of root.querySelectorAll?.('*') || []) {
        if (element.shadowRoot) {
          const nested = find(element.shadowRoot)
          if (nested) return nested
        }
      }
      return null
    }
    return Boolean(find(document)?.lastError_)
  }, null, {timeout: timeoutMs})
  return await readProfileProxyState(page, false)
}

export async function setProfileProxy(page, config) {
  await page.goto('brave://settings/privacy', {
    timeout: 30000,
    waitUntil: 'domcontentloaded',
  })
  await findProxyElement(page)
  return await page.evaluate(async config => {
    function find(root) {
      const direct = root.querySelector?.('settings-fingerprint-profile-proxy-subpage')
      if (direct) return direct
      for (const element of root.querySelectorAll?.('*') || []) {
        if (element.shadowRoot) {
          const nested = find(element.shadowRoot)
          if (nested) return nested
        }
      }
      return null
    }
    const element = find(document)
    element.setEnabledPref_(config.enabled)
    element.scheme_ = config.scheme
    element.host_ = config.host
    element.port_ = String(config.port || '')
    element.username_ = config.username || ''
    element.password_ = config.password || ''
    element.manualCountryCode_ = config.countryCode || ''
    element.manualTimezone_ = config.timezone || ''
    element.manualLatitude_ = config.latitude === undefined ? '' : String(config.latitude)
    element.manualLongitude_ = config.longitude === undefined ? '' : String(config.longitude)
    await element.onSave_(new Event('click'))
    return {
      conflictWarning: element.conflictWarning_,
      geoWarning: element.geoWarning_,
      hostError: element.hostError_,
      lastError: element.lastError_,
      portError: element.portError_,
      saveError: element.saveError_,
      savedStatus: element.savedStatus_,
    }
  }, config)
}

export async function renderProxyValidationError(page, screenshot) {
  await page.goto('brave://settings/privacy', {waitUntil: 'domcontentloaded'})
  await findProxyElement(page)
  const result = await page.evaluate(() => {
    function find(root) {
      const direct = root.querySelector?.('settings-fingerprint-profile-proxy-subpage')
      if (direct) return direct
      for (const element of root.querySelectorAll?.('*') || []) {
        if (element.shadowRoot) {
          const nested = find(element.shadowRoot)
          if (nested) return nested
        }
      }
      return null
    }
    const element = find(document)
    element.setEnabledPref_(true)
    element.host_ = ''
    element.port_ = '70000'
    const valid = element.validate_()
    return {
      hostError: element.hostError_,
      portError: element.portError_,
      valid,
    }
  })
  await page.screenshot({path: screenshot, fullPage: false})
  if (result.valid || !result.hostError || !result.portError) {
    throw new Error('Proxy settings validation state was not rendered')
  }
  return result
}

export async function collectProbe(page, origin) {
  await page.goto(origin, {waitUntil: 'domcontentloaded', timeout: 45000})
  await page.waitForFunction(() => window.__fpQaReady || window.__fpQaError, null, {
    timeout: 20000,
  })
  const result = await page.evaluate(async () => {
    if (window.__fpQaError) throw new Error(window.__fpQaError)
    const wireHeaders = await fetch('/headers.json', {cache: 'no-store'}).then(response => response.json())
    return {...window.__fpQaResult, wireHeaders}
  })
  assertProbeConsistency(result)
  return result
}

export function assertProbeConsistency(result) {
  const contexts = [
    ['iframe', result.iframe],
    ['dedicatedWorker', result.dedicatedWorker],
    ['sharedWorker', result.sharedWorker],
    ['serviceWorker', result.serviceWorker],
  ]
  for (const [name, context] of contexts) {
    if (!context || context.error || context.available === false) {
      throw new Error(`${name} fingerprint probe unavailable: ${JSON.stringify(context)}`)
    }
    for (const key of ['hardwareConcurrency', 'language', 'platform', 'userAgent']) {
      if (context[key] !== result.basic[key]) {
        throw new Error(`${name} ${key} did not match main document`)
      }
    }
    if (JSON.stringify(context.languages) !== JSON.stringify(result.basic.languages)) {
      throw new Error(`${name} languages did not match main document`)
    }
  }
  if (result.wireHeaders?.['user-agent'] !== result.basic.userAgent) {
    throw new Error('Wire User-Agent did not match navigator.userAgent')
  }
  const wireLanguages = String(result.wireHeaders?.['accept-language'] || '')
    .toLowerCase()
  if (!wireLanguages.includes(result.basic.language.toLowerCase())) {
    throw new Error('Wire Accept-Language did not include navigator.language')
  }
  const wirePlatform = result.wireHeaders?.['sec-ch-ua-platform']?.replaceAll('"', '')
  if (result.uaData?.platform && wirePlatform !== result.uaData.platform) {
    throw new Error('Wire UA-CH platform did not match navigator.userAgentData')
  }
  const wireBrands = [...String(result.wireHeaders?.['sec-ch-ua'] || '')
    .matchAll(/"([^"]+)";v="([^"]+)"/g)]
    .map(match => ({brand: match[1], version: match[2]}))
  for (const brand of result.uaData?.brands || []) {
    if (!wireBrands.some(value => value.brand === brand.brand &&
        value.version === brand.version)) {
      throw new Error(`Wire UA-CH brand did not match ${brand.brand}`)
    }
  }
  const wireFullVersions = [...String(
    result.wireHeaders?.['sec-ch-ua-full-version-list'] || '')
    .matchAll(/"([^"]+)";v="([^"]+)"/g)]
    .map(match => ({brand: match[1], version: match[2]}))
  for (const brand of result.uaData?.fullVersionList || []) {
    if (!wireFullVersions.some(value => value.brand === brand.brand &&
        value.version === brand.version)) {
      throw new Error(`Wire full UA-CH brand did not match ${brand.brand}`)
    }
  }
  const highEntropyHeaders = {
    architecture: 'sec-ch-ua-arch',
    bitness: 'sec-ch-ua-bitness',
    model: 'sec-ch-ua-model',
    platformVersion: 'sec-ch-ua-platform-version',
  }
  for (const [property, header] of Object.entries(highEntropyHeaders)) {
    if (property in (result.uaData || {})) {
      const wireValue = result.wireHeaders?.[header]?.replaceAll('"', '')
      if (wireValue !== result.uaData[property]) {
        throw new Error(`Wire UA-CH ${property} did not match navigator.userAgentData`)
      }
    }
  }
  if ('wow64' in (result.uaData || {})) {
    const expected = result.uaData.wow64 ? '?1' : '?0'
    if (result.wireHeaders?.['sec-ch-ua-wow64'] !== expected) {
      throw new Error('Wire UA-CH wow64 did not match navigator.userAgentData')
    }
  }
}

export async function readPersona(sourcePage, {screenshot = null} = {}) {
  const page = await sourcePage.context().newPage()
  try {
    await page.goto('brave://fingerprint-test/', {waitUntil: 'domcontentloaded'})
    await page.waitForFunction(() => document.querySelector('#status')?.dataset.state !== 'warn', null, {
      timeout: 15000,
    })
    if (screenshot) {
      await page.screenshot({path: screenshot, fullPage: false})
    }
    return await page.evaluate(() => {
      const summary = [...document.querySelectorAll('#summary .summary-item')]
      const entries = summary.map(item => [
        item.querySelector('span')?.textContent?.trim(),
        item.querySelector('.summary-value')?.textContent?.trim(),
      ])
      return {
        entries: Object.fromEntries(entries),
        state: document.querySelector('#status')?.dataset.state,
        status: document.querySelector('#status')?.textContent,
      }
    })
  } finally {
    await page.close()
  }
}

function stableFingerprint(probe) {
  return {
    audio: probe.audio,
    basic: probe.basic,
    canvas: probe.canvas,
    fonts: probe.fonts,
    screen: probe.screen,
    timezone: probe.timezone,
    uaData: probe.uaData,
    webgl: probe.webgl,
    webgpu: probe.webgpu,
  }
}

export async function runProfileLifecycle({config, dirs, probe, runId}) {
  const eventSets = []
  const sessions = []
  const profilePath = name => `/tmp/fingerprint-browser-${runId}/${name}`
  const launch = async name => {
    const session = await startQaSession({
      app: config.app,
      logDir: dirs.logs,
      name,
      profilePath: profilePath(name),
    })
    sessions.push(session)
    eventSets.push(session.events)
    return session
  }
  try {
    const sessionA = await launch('profile-a')
    const pageA = sessionA.context.pages()[0] || await sessionA.context.newPage()
    const firstA = await collectProbe(pageA, probe.origin)
    await pageA.evaluate(() => {
      localStorage.setItem('fp-qa-owner', 'profile-a')
    })
    await sessionA.context.addCookies([{
      expires: Math.floor(Date.now() / 1000) + 3600,
      name: 'fp-qa-owner',
      sameSite: 'Lax',
      url: probe.origin,
      value: 'profile-a',
    }])
    const initialStorageA = await pageA.evaluate(() => ({
      cookie: document.cookie,
      localStorage: localStorage.getItem('fp-qa-owner'),
    }))
    if (initialStorageA.localStorage !== 'profile-a' ||
        !initialStorageA.cookie.includes('profile-a')) {
      throw Object.assign(new Error('Profile A storage setup failed'), {
        details: {initialStorageA},
      })
    }
    const personaA = await readPersona(pageA)
    if (personaA.state !== 'pass') {
      throw new Error(`Profile A Persona comparison failed: ${personaA.status}`)
    }
    await sessionA.close()
    sessions.splice(sessions.indexOf(sessionA), 1)

    const restartedA = await launch('profile-a')
    const restartedPageA = restartedA.context.pages()[0] || await restartedA.context.newPage()
    const secondA = await collectProbe(restartedPageA, probe.origin)
    const storageA = await restartedPageA.evaluate(() => ({
      cookie: document.cookie,
      localStorage: localStorage.getItem('fp-qa-owner'),
    }))
    const restartedPersonaA = await readPersona(restartedPageA)

    const sessionB = await launch('profile-b')
    const pageB = sessionB.context.pages()[0] || await sessionB.context.newPage()
    const firstB = await collectProbe(pageB, probe.origin)
    const storageB = await pageB.evaluate(() => ({
      cookie: document.cookie,
      localStorage: localStorage.getItem('fp-qa-owner'),
    }))
    const personaB = await readPersona(pageB)
    if (personaB.state !== 'pass') {
      throw new Error(`Profile B Persona comparison failed: ${personaB.status}`)
    }

    const idA = personaA.entries['Persona ID']
    const restartedIdA = restartedPersonaA.entries['Persona ID']
    const idB = personaB.entries['Persona ID']
    if (!idA || idA !== restartedIdA) {
      throw new Error('Profile A Persona ID did not persist across restart')
    }
    if (!idB || idA === idB) {
      throw new Error('Profile A and B Persona IDs are not distinct')
    }
    if (JSON.stringify(stableFingerprint(firstA)) ===
        JSON.stringify(stableFingerprint(firstB))) {
      throw new Error('Profile A and B fingerprints are identical')
    }
    if (JSON.stringify(stableFingerprint(firstA)) !==
        JSON.stringify(stableFingerprint(secondA))) {
      throw new Error('Profile A fingerprint changed across restart')
    }
    if (storageA.localStorage !== 'profile-a' || !storageA.cookie.includes('profile-a')) {
      throw Object.assign(new Error('Profile A storage did not persist'), {
        details: {storageA},
      })
    }
    if (storageB.localStorage !== null || storageB.cookie.includes('profile-a')) {
      throw new Error('Profile B observed Profile A storage')
    }

    const pageScreenshot = path.join(dirs.page, 'full-profile-persona-b.png')
    await readPersona(pageB, {screenshot: pageScreenshot})

    await restartedA.close()
    sessions.splice(sessions.indexOf(restartedA), 1)
    await fs.rm(profilePath('profile-a'), {recursive: true, force: true})
    const recreatedA = await launch('profile-a')
    const recreatedPageA = recreatedA.context.pages()[0] ||
      await recreatedA.context.newPage()
    await collectProbe(recreatedPageA, probe.origin)
    const recreatedStorageA = await recreatedPageA.evaluate(() => ({
      cookie: document.cookie,
      localStorage: localStorage.getItem('fp-qa-owner'),
    }))
    const recreatedPersonaA = await readPersona(recreatedPageA)
    const recreatedIdA = recreatedPersonaA.entries['Persona ID']
    if (recreatedPersonaA.state !== 'pass' || !recreatedIdA || recreatedIdA === idA) {
      throw Object.assign(
        new Error('Deleted Profile A did not receive a fresh valid Persona'), {
          details: {idA, recreatedIdA, recreatedPersonaA},
        })
    }
    if (recreatedStorageA.localStorage !== null || recreatedStorageA.cookie) {
      throw new Error('Deleted Profile A retained site storage after recreation')
    }
    const failures = eventSets.flatMap(events => [
      ...events.browserExits.map(event => `browser exited ${event.code ?? event.signal}`),
      ...events.crashes.map(event => `renderer crash ${event.page}`),
      ...events.pageErrors.map(event => `pageerror ${event.message}`),
      ...events.disconnected.map(() => 'CDP disconnected'),
    ])
    if (failures.length > 0) {
      throw Object.assign(new Error(failures.join('; ')), {details: eventSets})
    }
    return {
      personaA: idA,
      personaAAfterDelete: recreatedIdA,
      personaB: idB,
      processes: [
        ...await recreatedA.processes(),
        ...await sessionB.processes(),
      ],
      screenshots: [pageScreenshot],
      storageA,
      storageB,
    }
  } finally {
    await Promise.all(sessions.map(session => session.close()))
  }
}

export async function collectWebRtcCandidates(page) {
  return await page.evaluate(async () => {
    const peer = new RTCPeerConnection({iceServers: [{urls: 'stun:stun.l.google.com:19302'}]})
    peer.createDataChannel('qa')
    const candidates = []
    peer.onicecandidate = event => {
      if (event.candidate) candidates.push(event.candidate.candidate)
    }
    await peer.setLocalDescription(await peer.createOffer())
    await new Promise(resolve => setTimeout(resolve, 5000))
    peer.close()
    return candidates
  })
}
