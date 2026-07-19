import fs from 'node:fs/promises'
import path from 'node:path'

import {
  auditCurrentPage,
  startQaExtensionSession,
  startQaSession,
} from './browser.mjs'
import {collectProbe} from './profile.mjs'
import {
  captureNativeScreenshot,
  clickNativeWindowOffset,
  clickNativeText,
  nativeKeyCode,
  nativeScreenshotHasText,
  pathExists,
  setFrontWindowPosition,
} from './system.mjs'

function webStoreExtensionId(extensionUrl) {
  const parsed = new URL(extensionUrl)
  const id = parsed.pathname.split('/').filter(Boolean).at(-1)
  if (parsed.protocol !== 'https:' || parsed.hostname !== 'chromewebstore.google.com' ||
      !/^[a-p]{32}$/.test(id || '')) {
    throw new Error(`Invalid Chrome Web Store extension URL: ${extensionUrl}`)
  }
  return id
}

async function extensionItems(page) {
  await page.goto('brave://extensions/', {waitUntil: 'domcontentloaded'})
  await page.waitForTimeout(1000)
  return await page.evaluate(() => {
    function all(root, selector, found = []) {
      found.push(...root.querySelectorAll?.(selector) || [])
      for (const element of root.querySelectorAll?.('*') || []) {
        if (element.shadowRoot) all(element.shadowRoot, selector, found)
      }
      return found
    }
    return all(document, 'extensions-item').map(item => ({
      disableReasons: item.data?.disableReasons || null,
      id: item.data?.id || item.getAttribute('id'),
      name: item.data?.name || '',
      state: item.data?.state || '',
    }))
  })
}

async function enableExtensionDeveloperMode(page) {
  await page.goto('brave://extensions/', {waitUntil: 'domcontentloaded'})
  const updated = await page.evaluate(async () => {
    const api = chrome.developerPrivate
    if (!api?.updateProfileConfiguration || !api?.getProfileConfiguration) {
      return false
    }
    const profile = await api.getProfileConfiguration()
    if (!profile.inDeveloperMode) {
      await api.updateProfileConfiguration({inDeveloperMode: true})
    }
    return true
  })
  if (!updated) throw new Error('Could not access extension Developer Mode API')
  await page.waitForFunction(async () => {
    const api = chrome.developerPrivate
    return Boolean((await api?.getProfileConfiguration?.())?.inDeveloperMode)
  }, null, {timeout: 10000})
}

async function clickExtensionControl(page, extensionId, selector) {
  await page.goto('brave://extensions/', {waitUntil: 'domcontentloaded'})
  return await page.evaluate(({extensionId, selector}) => {
    function all(root, target, found = []) {
      found.push(...root.querySelectorAll?.(target) || [])
      for (const element of root.querySelectorAll?.('*') || []) {
        if (element.shadowRoot) all(element.shadowRoot, target, found)
      }
      return found
    }
    const item = all(document, 'extensions-item')
      .find(candidate => candidate.data?.id === extensionId || candidate.id === extensionId)
    const control = item?.shadowRoot?.querySelector(selector)
    if (!control) return false
    control.click()
    return true
  }, {extensionId, selector})
}

async function extensionEnabled(page, extensionId) {
  await page.goto('brave://extensions/', {waitUntil: 'domcontentloaded'})
  return await page.evaluate(extensionId => {
    function all(root, target, found = []) {
      found.push(...root.querySelectorAll?.(target) || [])
      for (const element of root.querySelectorAll?.('*') || []) {
        if (element.shadowRoot) all(element.shadowRoot, target, found)
      }
      return found
    }
    const item = all(document, 'extensions-item')
      .find(candidate => candidate.data?.id === extensionId || candidate.id === extensionId)
    return item?.shadowRoot?.querySelector('#enableToggle')?.checked ?? null
  }, extensionId)
}

async function setExtensionEnabled(page, extensionId, enabled) {
  await page.goto('brave://extensions/', {waitUntil: 'domcontentloaded'})
  await page.evaluate(
    ({extensionId, enabled}) => chrome.management.setEnabled(extensionId, enabled),
    {extensionId, enabled},
  )
  await waitForEnabled(page, extensionId, enabled)
}

async function waitForEnabled(page, extensionId, expected, timeoutMs = 10000) {
  const deadline = Date.now() + timeoutMs
  while (Date.now() < deadline) {
    if (await extensionEnabled(page, extensionId) === expected) return
    await page.waitForTimeout(250)
  }
  const items = await extensionItems(page)
  throw Object.assign(
    new Error(`Extension ${extensionId} enabled state did not become ${expected}`),
    {details: {expected, items}},
  )
}

async function waitForExtension(page, predicate, timeoutMs = 20000) {
  const deadline = Date.now() + timeoutMs
  while (Date.now() < deadline) {
    const items = await extensionItems(page)
    const match = items.find(predicate)
    if (match) return match
    await page.waitForTimeout(500)
  }
  throw new Error('Timed out waiting for extension state')
}

async function waitForExtensionRemoved(page, extensionId) {
  const deadline = Date.now() + 10000
  while (Date.now() < deadline) {
    const remaining = await extensionItems(page)
    if (!remaining.some(item => item.id === extensionId)) return
    await page.waitForTimeout(250)
  }
  throw new Error(`Extension ${extensionId} remained after uninstall`)
}

async function removeWebStoreExtension(page, extensionId, screenshot, pid) {
  await page.goto('brave://extensions/', {waitUntil: 'domcontentloaded'})
  await page.evaluate(extensionId => {
    chrome.management.uninstall(extensionId, {showConfirmDialog: true})
      .catch(() => {})
  }, extensionId)
  await new Promise(resolve => setTimeout(resolve, 1200))
  await captureNativeScreenshot(screenshot, pid)
  await clickNativeText(screenshot, 'Remove', pid)
  await waitForExtensionRemoved(page, extensionId)
}

async function findManifest(profilePath, extensionId) {
  const root = path.join(profilePath, 'Default', 'Extensions', extensionId)
  if (!(await pathExists(root))) return null
  const versions = await fs.readdir(root, {withFileTypes: true})
  for (const version of versions.filter(entry => entry.isDirectory()).reverse()) {
    const manifestFile = path.join(root, version.name, 'manifest.json')
    if (await pathExists(manifestFile)) {
      return JSON.parse(await fs.readFile(manifestFile, 'utf8'))
    }
  }
  return null
}

async function captureExtensionPages(page, extensionId, manifest, pageDir, prefix) {
  const screenshots = []
  const targets = [
    ['popup', manifest?.action?.default_popup || manifest?.browser_action?.default_popup],
    ['options', manifest?.options_page || manifest?.options_ui?.page],
  ]
  for (const [name, resource] of targets) {
    if (!resource) continue
    const extensionUrl =
      `chrome-extension://${extensionId}/${resource.replace(/^\//, '')}`
    await page.goto(extensionUrl, {waitUntil: 'domcontentloaded'})
    const screenshot = path.join(pageDir, `${prefix}-${name}.png`)
    await page.screenshot({path: screenshot, fullPage: false})
    screenshots.push(screenshot)
  }
  return screenshots
}

async function openExtensionsMenu({
  extensionName,
  menuScreenshot,
  pid,
  toolbarScreenshot,
}) {
  const attempts = [
    async () => await clickNativeText(toolbarScreenshot, 'Extensions', pid),
    async () => await clickNativeWindowOffset(170, 60, pid),
    async () => await clickNativeWindowOffset(180, 60, pid),
  ]
  let lastError
  for (const open of attempts) {
    try {
      await open()
      await new Promise(resolve => setTimeout(resolve, 600))
      await captureNativeScreenshot(menuScreenshot, pid)
      if (await nativeScreenshotHasText(menuScreenshot, extensionName)) return
    } catch (error) {
      lastError = error
    }
    await nativeKeyCode(53, pid).catch(() => {})
    await new Promise(resolve => setTimeout(resolve, 200))
  }
  throw new Error(
    `Extensions menu did not show ${extensionName}` +
      (lastError ? `: ${lastError.message}` : ''))
}

async function captureWebStoreExtensionPages({
  page,
  extension,
  manifest,
  dirs,
  prefix,
  pid,
}) {
  const screenshots = []
  const popup = manifest?.action?.default_popup ||
    manifest?.browser_action?.default_popup
  if (popup) {
    await page.goto('https://example.com/', {waitUntil: 'domcontentloaded'})
    await page.bringToFront()
    await new Promise(resolve => setTimeout(resolve, 300))
    const toolbarScreenshot = path.join(
      dirs.native, `${prefix}-extensions-menu-button.png`)
    await captureNativeScreenshot(toolbarScreenshot, pid)
    const menuScreenshot = path.join(dirs.native, `${prefix}-extensions-menu.png`)
    await openExtensionsMenu({
      extensionName: extension.name,
      menuScreenshot,
      pid,
      toolbarScreenshot,
    })
    await clickNativeText(menuScreenshot, extension.name, pid)
    await new Promise(resolve => setTimeout(resolve, 800))
    const popupScreenshot = path.join(dirs.native, `${prefix}-popup.png`)
    await captureNativeScreenshot(popupScreenshot, pid)
    screenshots.push(menuScreenshot, popupScreenshot)
    await nativeKeyCode(53, pid)
  }

  const options = manifest?.options_page || manifest?.options_ui?.page
  if (options) {
    await page.goto(
      `chrome-extension://${extension.id}/${options.replace(/^\//, '')}`,
      {waitUntil: 'domcontentloaded'},
    )
    const optionsScreenshot = path.join(dirs.page, `${prefix}-options.png`)
    await page.screenshot({path: optionsScreenshot, fullPage: false})
    screenshots.push(optionsScreenshot)
  }
  return screenshots
}

async function sendExtensionMessage(page, extensionId, message, timeoutMs = 10000) {
  const deadline = Date.now() + timeoutMs
  let lastError
  while (Date.now() < deadline) {
    try {
      await page.goto(`chrome-extension://${extensionId}/popup.html`, {
        waitUntil: 'domcontentloaded',
      })
      return await page.evaluate(async message =>
        await chrome.runtime.sendMessage(message), message)
    } catch (error) {
      lastError = error
      if (!String(error).includes('ERR_BLOCKED_BY_CLIENT')) throw error
      await page.waitForTimeout(250)
    }
  }
  throw lastError
}

function assertWorkerFingerprint(worker, main) {
  for (const key of ['hardwareConcurrency', 'language', 'platform', 'userAgent']) {
    if (worker?.[key] !== main.basic[key]) {
      throw new Error(`MV3 service worker ${key} did not match main document`)
    }
  }
  if (JSON.stringify(worker.languages) !== JSON.stringify(main.basic.languages)) {
    throw new Error('MV3 service worker languages did not match main document')
  }
}

function assertSessionHealth(events, label) {
  const failures = [
    ...events.browserExits.map(event => `browser exited ${event.code ?? event.signal}`),
    ...events.crashes.map(event => `renderer crash ${event.page}`),
    ...events.pageErrors.map(event => `pageerror ${event.message}`),
    ...events.disconnected.map(() => 'CDP disconnected'),
  ]
  if (failures.length > 0) {
    throw Object.assign(new Error(`${label}: ${failures.join('; ')}`), {
      details: events,
    })
  }
}

export async function runLocalExtensionLifecycle({config, dirs, fixtureDir, probe, runId}) {
  const profilePath = `/tmp/fingerprint-browser-${runId}/local-extension`
  const eventSets = []
  let session = await startQaExtensionSession({
    app: config.app,
    logDir: dirs.logs,
    name: 'local-extension',
    profilePath,
  })
  eventSets.push(session.events)
  try {
    let page = session.context.pages()[0] || await session.context.newPage()
    await enableExtensionDeveloperMode(page)
    const loaded = await session.browserSession.send(
      'Extensions.loadUnpacked', {path: fixtureDir})
    const extension = await waitForExtension(
      page, item => item.id === loaded.id && item.name === 'Fingerprint QA MV3')
    const mainFingerprint = await collectProbe(page, probe.origin)
    const workerFingerprint = await sendExtensionMessage(
      page, extension.id, {type: 'collect'})
    assertWorkerFingerprint(workerFingerprint, mainFingerprint)
    const screenshots = await captureExtensionPages(
      page,
      extension.id,
      {action: {default_popup: 'popup.html'}, options_page: 'options.html'},
      dirs.page,
      'full-local-extension')

    const conflict = await sendExtensionMessage(
      page, extension.id, {type: 'set-proxy-conflict'})
    if (conflict?.error) {
      throw new Error(`MV3 proxy conflict setup failed: ${conflict.error}`)
    }
    await page.goto(
      'brave://settings/fingerprintProfileProxy',
      {waitUntil: 'domcontentloaded'})
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
      const element = find(document)
      return Boolean(
        element?.state_ === 'conflict' && element?.statusMessage_)
    }, null, {timeout: 15000})
    const conflictScreenshot = path.join(
      dirs.page, 'full-local-extension-proxy-conflict.png')
    await page.screenshot({path: conflictScreenshot, fullPage: false})
    const conflictAudit = await auditCurrentPage(page)
    if (conflictAudit.componentContrastFailures.length > 0 ||
        conflictAudit.contrastFailures.length > 0 ||
        conflictAudit.layoutFailures.length > 0) {
      throw Object.assign(new Error('Proxy conflict state failed WebUI checks'), {
        details: conflictAudit,
      })
    }
    screenshots.push(conflictScreenshot)
    const cleared = await sendExtensionMessage(
      page, extension.id, {type: 'clear-proxy-conflict'})
    if (cleared?.error) {
      throw new Error(`MV3 proxy conflict cleanup failed: ${cleared.error}`)
    }

    await page.goto('brave://extensions/', {waitUntil: 'domcontentloaded'})
    if (!(await clickExtensionControl(page, extension.id, '#dev-reload-button'))) {
      throw new Error('Extension reload control missing')
    }
    await waitForEnabled(page, extension.id, true)
    const reloadedFingerprint = await sendExtensionMessage(
      page, extension.id, {type: 'collect'})
    assertWorkerFingerprint(reloadedFingerprint, mainFingerprint)
    await setExtensionEnabled(page, extension.id, false)
    await setExtensionEnabled(page, extension.id, true)

    await session.close()
    session = await startQaExtensionSession({
      app: config.app,
      logDir: dirs.logs,
      name: 'local-extension-restart',
      profilePath,
    })
    eventSets.push(session.events)
    page = session.context.pages()[0] || await session.context.newPage()
    await waitForExtension(page, item => item.id === extension.id)
    const restartedFingerprint = await sendExtensionMessage(
      page, extension.id, {type: 'collect'})
    assertWorkerFingerprint(restartedFingerprint, mainFingerprint)
    if (restartedFingerprint.canvas !== workerFingerprint.canvas) {
      throw new Error('MV3 service worker canvas changed across restart')
    }
    await session.browserSession.send(
      'Extensions.uninstall', {id: extension.id})
    page = session.context.pages().find(candidate => !candidate.isClosed()) ||
      await session.context.newPage()
    await waitForExtensionRemoved(page, extension.id)
    if ((await session.processes()).length === 0) {
      throw new Error('Brave exited after local extension uninstall')
    }
    for (const events of eventSets) assertSessionHealth(events, 'Local MV3 lifecycle')
    return {
      extension,
      screenshots,
      workerFingerprint,
    }
  } finally {
    await session.close()
  }
}

async function confirmNativeInstall(screenshot, pid) {
  await new Promise(resolve => setTimeout(resolve, 1200))
  await setFrontWindowPosition(100, 100, pid)
  await new Promise(resolve => setTimeout(resolve, 300))
  await captureNativeScreenshot(screenshot, pid)
  await clickNativeText(screenshot, 'Add extension', pid)
}

export async function runWebStoreExtensionLifecycle({
  config,
  dirs,
  extensionUrl,
  label,
  runId,
}) {
  const expectedExtensionId = webStoreExtensionId(extensionUrl)
  const profilePath = `/tmp/fingerprint-browser-${runId}/cws-${label}`
  const eventSets = []
  let session = await startQaSession({
    app: config.app,
    extraArgs: [
      '--enable-logging=stderr',
      '--vmodule=webstore_installer=2,webstore_private_api=2,extension_downloader=2,crx_installer=2,sandboxed_unpacker=2',
    ],
    logDir: dirs.logs,
    name: `cws-${label}`,
    profilePath,
    testType: false,
  })
  eventSets.push(session.events)
  try {
    let page = session.context.pages()[0] || await session.context.newPage()
    const before = await extensionItems(page)
    await page.goto(extensionUrl, {waitUntil: 'domcontentloaded', timeout: 60000})
    await page.evaluate(() => {
      const api = chrome.webstorePrivate
      const original = api.completeInstall.bind(api)
      globalThis.__fingerprintQaWebStoreError = null
      api.completeInstall = (...args) => {
        const callbackIndex = args.findIndex(value => typeof value === 'function')
        if (callbackIndex >= 0) {
          const callback = args[callbackIndex]
          args[callbackIndex] = (...values) => {
            globalThis.__fingerprintQaWebStoreError =
              chrome.runtime.lastError?.message || null
            return callback(...values)
          }
        }
        return original(...args)
      }
    })
    const button = page.getByRole('button', {name: /Add to (Brave|Chrome)/i}).first()
    await button.waitFor({state: 'visible', timeout: 30000})
    await button.click()
    const confirmationScreenshot = path.join(
      dirs.native, `full-cws-${label}-install-confirmation.png`)
    await confirmNativeInstall(confirmationScreenshot, session.process.child.pid)
    let installed
    const statePage = await session.context.newPage()
    try {
      installed = await waitForExtension(
        statePage,
        item => item.id === expectedExtensionId &&
          !before.some(existing => existing.id === item.id),
        30000)
    } catch (error) {
      const blockedScreenshot = path.join(
        dirs.native, `full-cws-${label}-install-confirmation-blocked.png`)
      await captureNativeScreenshot(blockedScreenshot, session.process.child.pid)
      if (await nativeScreenshotHasText(blockedScreenshot, 'Add extension')) {
        return {
          reason: 'macOS blocked native QA input; grant Accessibility/Input Monitoring before Chrome Web Store automation',
          screenshots: [confirmationScreenshot, blockedScreenshot],
          status: 'BLOCKED',
        }
      }
      const installError = page.isClosed() ? null : await page.evaluate(
        () => globalThis.__fingerprintQaWebStoreError)
      if (installError) {
        throw new Error(`Chrome Web Store install failed: ${installError}`)
      }
      throw error
    } finally {
      await statePage.close().catch(() => {})
    }
    if (page.isClosed()) page = await session.context.newPage()
    const manifest = await findManifest(profilePath, installed.id)
    const screenshots = [confirmationScreenshot, ...await captureWebStoreExtensionPages({
      page,
      extension: installed,
      manifest,
      dirs,
      prefix: `full-cws-${label}`,
      pid: session.process.child.pid,
    })]
    const toolbarScreenshot = path.join(dirs.page, `full-cws-${label}-extensions.png`)
    await page.goto('brave://extensions/', {waitUntil: 'domcontentloaded'})
    await page.screenshot({path: toolbarScreenshot, fullPage: false})
    screenshots.push(toolbarScreenshot)
    await page.goto(extensionUrl, {waitUntil: 'domcontentloaded', timeout: 60000})
    await page.bringToFront()
    const nativeToolbarScreenshot = path.join(
      dirs.native, `full-cws-${label}-installed-toolbar.png`)
    await captureNativeScreenshot(nativeToolbarScreenshot, session.process.child.pid)
    screenshots.push(nativeToolbarScreenshot)

    await session.close()
    session = await startQaSession({
      app: config.app,
      extraArgs: [
        '--enable-logging=stderr',
        '--vmodule=webstore_installer=2,webstore_private_api=2,extension_downloader=2,crx_installer=2,sandboxed_unpacker=2',
      ],
      logDir: dirs.logs,
      name: `cws-${label}-restart`,
      profilePath,
      testType: false,
    })
    eventSets.push(session.events)
    page = await session.context.newPage()
    await waitForExtension(page, item => item.id === installed.id)
    await setExtensionEnabled(page, installed.id, false)
    await setExtensionEnabled(page, installed.id, true)
    const uninstallScreenshot = path.join(
      dirs.native, `full-cws-${label}-uninstall-confirmation.png`)
    await removeWebStoreExtension(
      page, installed.id, uninstallScreenshot, session.process.child.pid)
    screenshots.push(uninstallScreenshot)
    if ((await session.processes()).length === 0) {
      throw new Error(`Brave exited after uninstalling ${installed.id}`)
    }
    for (const events of eventSets) assertSessionHealth(events, `${label} CWS lifecycle`)
    return {extension: installed, screenshots}
  } finally {
    await session.close()
  }
}
