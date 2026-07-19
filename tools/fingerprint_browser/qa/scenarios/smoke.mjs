import fs from 'node:fs/promises'
import path from 'node:path'

import {navigateAndCapture} from '../lib/browser.mjs'
import {collectProbe} from '../lib/profile.mjs'
import {runScenario} from '../lib/report.mjs'
import {analyzeScreenshot} from '../lib/visual.mjs'
import {captureNativeScreenshot, nativeShortcut} from '../lib/system.mjs'

const PAGE_CASES = [
  {id: 'smoke-new-tab', name: 'new-tab', url: 'brave://newtab/'},
  {id: 'smoke-google', name: 'google', url: 'https://www.google.com/'},
  {id: 'smoke-facebook', name: 'facebook', url: 'https://www.facebook.com/'},
  {id: 'smoke-wikipedia', name: 'wikipedia', url: 'https://www.wikipedia.org/'},
  {id: 'smoke-settings', name: 'settings', url: 'brave://settings/'},
]

function fatalConsoleMessages(messages) {
  return messages.filter(message =>
    /CHECK failed|FATAL|DYLD|Aw, Snap|crash/i.test(message.text))
}

export async function runSmoke({config, dirs, probe, report, session}) {
  const pages = session.context.pages()
  const page = pages[0] || await session.context.newPage()
  await page.setViewportSize({width: 1280, height: 800})
  const pageResults = []
  const visualResults = []
  let fingerprintPage = null

  for (const item of PAGE_CASES) {
    await runScenario(report, item.id, async () => {
      const screenshot = path.join(dirs.page, `smoke-${item.name}-1280x800.png`)
      const result = await navigateAndCapture({
        page,
        screenshot,
        url: item.url,
        validateContrast: item.url.startsWith('brave://settings'),
        validateLayout: item.url.startsWith('brave://settings'),
      })
      if (result.contrastFailures.length > 0) {
        throw Object.assign(new Error(
          `${result.contrastFailures.length} visible text nodes below 4.5:1 contrast`), {
          details: result.contrastFailures.slice(0, 20),
        })
      }
      if (result.componentContrastFailures.length > 0) {
        throw Object.assign(new Error(
          `${result.componentContrastFailures.length} icons or controls below 3:1 contrast`), {
          details: result.componentContrastFailures.slice(0, 20),
        })
      }
      if (result.layoutFailures.length > 0) {
        throw Object.assign(new Error(
          `${result.layoutFailures.length} WebUI layout failures`), {
          details: result.layoutFailures.slice(0, 20),
        })
      }
      pageResults.push(result)
      return {screenshots: [screenshot], url: result.finalUrl}
    })
  }

  await runScenario(report, 'smoke-fingerprint-probe', async () => {
    const probeScreenshot = path.join(dirs.page, 'smoke-fingerprint-probe-1280x800.png')
    const probeResult = await navigateAndCapture({
      page,
      screenshot: probeScreenshot,
      url: probe.origin,
      validateContrast: true,
      validateLayout: true,
    })
    const fingerprint = await collectProbe(page, probe.origin)
    await page.screenshot({path: probeScreenshot, fullPage: false})
    if (probeResult.contrastFailures.length > 0) {
      throw new Error('Fingerprint probe has text below 4.5:1 contrast')
    }

    fingerprintPage = await session.context.newPage()
    await fingerprintPage.setViewportSize({width: 1280, height: 800})
    const testScreenshot = path.join(dirs.page, 'smoke-fingerprint-test-1280x800.png')
    const testResult = await navigateAndCapture({
      page: fingerprintPage,
      screenshot: testScreenshot,
      url: 'brave://fingerprint-test/',
      validateContrast: true,
      validateLayout: true,
      waitAfterMs: 1800,
    })
    const status = await fingerprintPage.locator('#status').textContent()
    const state = await fingerprintPage.locator('#status').getAttribute('data-state')
    if (state !== 'pass') {
      throw new Error(`Fingerprint test did not fully match: ${status}`)
    }
    if (testResult.contrastFailures.length > 0) {
      throw new Error('Fingerprint test has text below 4.5:1 contrast')
    }
    const componentContrastFailures = [
      ...probeResult.componentContrastFailures,
      ...testResult.componentContrastFailures,
    ]
    if (componentContrastFailures.length > 0) {
      throw Object.assign(new Error('Fingerprint pages have icons or controls below 3:1 contrast'), {
        details: componentContrastFailures,
      })
    }
    const layoutFailures = [
      ...probeResult.layoutFailures,
      ...testResult.layoutFailures,
    ]
    if (layoutFailures.length > 0) {
      throw Object.assign(new Error('Fingerprint pages have layout failures'), {
        details: layoutFailures,
      })
    }
    pageResults.push(probeResult, testResult)
    return {
      fingerprint,
      fingerprintStatus: status,
      screenshots: [probeScreenshot, testScreenshot],
      url: testResult.finalUrl,
    }
  })

  await runScenario(report, 'smoke-navigation-and-tabs', async () => {
    await page.goto(`${probe.origin}/iframe.html?state=one`, {waitUntil: 'load'})
    await page.goto(`${probe.origin}/iframe.html?state=two`, {waitUntil: 'load'})
    for (let iteration = 0; iteration < 10; iteration += 1) {
      await page.goBack({waitUntil: 'commit', timeout: 5000})
      await page.goForward({waitUntil: 'commit', timeout: 5000})
      await page.reload({waitUntil: 'domcontentloaded'})
    }
    const temporary = []
    for (let index = 0; index < 3; index += 1) {
      const tab = await session.context.newPage()
      await tab.goto(`${probe.origin}/iframe.html?tab=${index}`, {waitUntil: 'load'})
      temporary.push(tab)
    }
    const last = temporary.pop()
    const restoredUrl = last.url()
    await last.bringToFront()
    await last.close()
    let restoredPage
    for (let attempt = 0; attempt < 2 && !restoredPage; attempt += 1) {
      const restoredPagePromise = session.context.waitForEvent(
        'page', {timeout: 10000})
      await nativeShortcut('t', ['command', 'shift'], session.process.child.pid)
      try {
        restoredPage = await restoredPagePromise
      } catch (error) {
        if (attempt === 1) {
          throw error
        }
      }
    }
    await restoredPage.waitForURL(restoredUrl, {timeout: 10000})
    if (restoredPage.url() !== restoredUrl) {
      throw new Error(
        `Closed tab was not restored: expected ${restoredUrl}, got ${restoredPage.url()}`)
    }
    for (const tab of temporary) {
      await tab.close()
    }
    return {
      iterations: 10,
      openPages: session.context.pages().length,
    }
  })

  await runScenario(report, 'smoke-native-screenshot', async () => {
    if (!fingerprintPage || fingerprintPage.isClosed()) {
      throw new Error('Fingerprint evidence tab is unavailable')
    }
    await fingerprintPage.bringToFront()
    await fingerprintPage.waitForTimeout(500)
    const screenshot = path.join(dirs.native, 'smoke-browser-chrome-1280x800.png')
    await captureNativeScreenshot(screenshot, session.process.child.pid)
    const visual = await analyzeScreenshot({
      actual: screenshot,
      baselineKey: path.join('native', path.basename(screenshot)),
      baselineDir: config.baselineDir,
      checkRed: true,
      diffDir: dirs.diff,
    })
    visualResults.push(visual)
    if (!visual.pass) {
      throw Object.assign(new Error(visual.reason), {details: visual})
    }
    return {screenshots: [screenshot], visual}
  })

  await runScenario(report, 'smoke-runtime-health', async () => {
    const processes = await session.processes()
    const fatalConsole = fatalConsoleMessages(session.events.console)
    const failures = [
      ...session.events.browserExits.map(event =>
        `browser exited: ${event.code ?? event.signal}`),
      ...session.events.crashes.map(event => `renderer crash: ${event.page}`),
      ...session.events.pageErrors.map(event => `pageerror: ${event.message}`),
      ...session.events.disconnected.map(() => 'CDP disconnected'),
      ...fatalConsole.map(event => `fatal console: ${event.text}`),
    ]
    if (failures.length > 0) {
      throw Object.assign(new Error(failures.join('; ')), {
        details: session.events,
      })
    }
    await fs.writeFile(
      path.join(dirs.logs, 'browser-events.json'),
      `${JSON.stringify(session.events, null, 2)}\n`)
    return {
      processes,
      runtimeEvents: session.events,
    }
  })

  return {pageResults, visualResults}
}
