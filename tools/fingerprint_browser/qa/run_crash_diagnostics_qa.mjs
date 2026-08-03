// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

import fs from 'node:fs/promises'
import path from 'node:path'

import { parseArgs, usage } from './lib/args.mjs'
import { prepareAndVerifyArtifacts } from './lib/artifacts.mjs'
import { navigateAndCapture, startQaSession } from './lib/browser.mjs'
import { inspectDiagnosticsBundle } from './lib/diagnostics.mjs'
import {
  assertNativeUiFocusRetained,
  beginNativeUiSession,
  captureNativeScreenshot,
  clickNativeText,
  clickNativeWindowOffset,
  copyCrashReports,
  endNativeUiSession,
  nativeScreenshotHasText,
  newCrashReports,
  safeRunId,
  snapshotCrashReports,
  stopProfileProcesses,
} from './lib/system.mjs'
import { analyzeScreenshot, writeVisualReviewBundle } from './lib/visual.mjs'

const CRASH_URLS = {
  renderer: 'chrome://crash/',
}

const delay = (milliseconds) =>
  new Promise((resolve) => setTimeout(resolve, milliseconds))

async function waitForNewReports(crashpadDir, before, timeoutMs = 30000) {
  const deadline = Date.now() + timeoutMs
  while (Date.now() < deadline) {
    const after = await snapshotCrashReports({
      crashpadDirectories: [crashpadDir],
      diagnosticReportsDirectory: path.join(crashpadDir, '__native-disabled__'),
    })
    const reports = newCrashReports(before, after)
    if (reports.length > 0) return reports
    await delay(250)
  }
  throw new Error(`Crashpad did not produce a report within ${timeoutMs} ms`)
}

async function crashSnapshot(crashpadDir) {
  return await snapshotCrashReports({
    crashpadDirectories: [crashpadDir],
    diagnosticReportsDirectory: path.join(crashpadDir, '__native-disabled__'),
  })
}

async function induceChildCrash(session, type, crashpadDir) {
  const before = await crashSnapshot(crashpadDir)
  let page = null
  if (type === 'renderer') {
    page = await session.context.newPage()
    const crashed = page.waitForEvent('crash', { timeout: 15000 })
    await page
      .goto(CRASH_URLS.renderer, {
        timeout: 10000,
        waitUntil: 'commit',
      })
      .catch(() => {})
    await crashed
  } else if (type === 'gpu') {
    await session.browserSession.send('Browser.crashGpuProcess')
  } else {
    throw new Error(`Unsupported child crash type: ${type}`)
  }
  const reports = await waitForNewReports(crashpadDir, before)
  await page?.close().catch(() => {})
  return reports
}

async function induceBrowserCrash(session, crashpadDir) {
  const before = await crashSnapshot(crashpadDir)
  const page = session.context.pages()[0] || (await session.context.newPage())
  await page.goto('brave://diagnostics/', { waitUntil: 'domcontentloaded' })
  await page
    .evaluate(() => {
      globalThis.chrome.send('crashDiagnosticsForTesting')
    })
    .catch(() => {})
  const exit = await Promise.race([
    session.process.exit,
    delay(15000).then(() => {
      throw new Error('Browser did not exit after the controlled crash')
    }),
  ])
  const reports = await waitForNewReports(crashpadDir, before)
  return { exit, reports }
}

async function saveDiagnosticsBundle(
  session,
  outputDir,
  screenshotsDir,
  screenshots,
) {
  const page = session.context.pages()[0] || (await session.context.newPage())
  await page.goto('brave://diagnostics/?scope=latest_incident', {
    waitUntil: 'domcontentloaded',
  })
  await page.locator('#privacy-confirm').check()
  await page.locator('#export').click()
  await delay(1500)
  const savePanel = path.join(screenshotsDir, 'export-save-panel-native.png')
  await captureNativeScreenshot(savePanel, session.process.child.pid)
  screenshots.push(savePanel)
  await clickNativeText(savePanel, 'Save', session.process.child.pid)
  try {
    await page.locator('#export-status[data-state="success"]').waitFor({
      timeout: 120000,
    })
  } catch (error) {
    const saveFailure = path.join(
      screenshotsDir,
      'export-save-failure-native.png',
    )
    await captureNativeScreenshot(saveFailure, session.process.child.pid)
      .then(() => screenshots.push(saveFailure))
      .catch(() => {})
    throw error
  }
  const archives = (await fs.readdir(outputDir))
    .filter(
      (name) =>
        name.startsWith('Fingerprint-Browser-Diagnostics-')
        && name.endsWith('.zip'),
    )
    .sort()
  if (archives.length !== 1) {
    throw new Error(`Expected one diagnostic archive, found ${archives.length}`)
  }
  return path.join(outputDir, archives[0])
}

async function capturePageMatrix(session, screenshotsDir) {
  const screenshots = []
  const audits = []
  const page = session.context.pages()[0] || (await session.context.newPage())
  for (const colorScheme of ['light', 'dark']) {
    await page.emulateMedia({ colorScheme })
    for (const [width, height] of [
      [1024, 768],
      [1280, 800],
      [1512, 982],
    ]) {
      await page.setViewportSize({ width, height })
      for (const [name, url] of [
        ['diagnostics', 'brave://diagnostics/'],
        ['crashes', 'brave://crashes/'],
      ]) {
        const target = path.join(
          screenshotsDir,
          `${name}-${colorScheme}-${width}x${height}.png`,
        )
        const audit = await navigateAndCapture({
          colorScheme,
          page,
          screenshot: target,
          url,
          validateContrast: true,
          validateLayout: true,
        })
        audits.push({ colorScheme, height, name, width, ...audit })
        screenshots.push(target)
      }
    }
  }
  return { audits, screenshots }
}

async function captureHelpMenu(session, screenshotsDir, colorScheme) {
  const page = session.context.pages()[0] || (await session.context.newPage())
  await page.goto('brave://diagnostics/', { waitUntil: 'domcontentloaded' })
  await assertNativeUiFocusRetained(session.process.child.pid)
  await page.bringToFront()
  await delay(500)
  const toolbar = path.join(
    screenshotsDir,
    `help-toolbar-${colorScheme}-native.png`,
  )
  await captureNativeScreenshot(toolbar, session.process.child.pid)
  await clickNativeText(
    toolbar,
    'Customize and control Brave',
    session.process.child.pid,
  ).catch(async () => {
    await clickNativeWindowOffset(30, 60, session.process.child.pid)
  })
  await delay(500)
  const menu = path.join(screenshotsDir, `help-menu-${colorScheme}-native.png`)
  await captureNativeScreenshot(menu, session.process.child.pid)
  if (!(await nativeScreenshotHasText(menu, 'Help'))) {
    throw new Error('Brave app menu did not expose Help')
  }
  await clickNativeText(menu, 'Help', session.process.child.pid)
  await delay(500)
  const submenu = path.join(
    screenshotsDir,
    `help-submenu-${colorScheme}-native.png`,
  )
  await captureNativeScreenshot(submenu, session.process.child.pid)
  if (!(await nativeScreenshotHasText(submenu, '导出诊断信息'))) {
    throw new Error('帮助菜单未显示“导出诊断信息”')
  }
  return [toolbar, menu, submenu]
}

async function captureRecoveryUi(session, screenshotsDir, colorScheme) {
  const initial = path.join(
    screenshotsDir,
    `recovery-initial-${colorScheme}-native.png`,
  )
  let initialReady = false
  for (let attempt = 0; attempt < 20; attempt += 1) {
    await delay(500)
    await captureNativeScreenshot(initial, session.process.child.pid)
    initialReady = await nativeScreenshotHasText(initial, '导出诊断信息')
    if (initialReady) break
  }
  if (!initialReady) {
    throw new Error('Recovery UI lacked diagnostics export')
  }
  const recovery = path.join(
    screenshotsDir,
    `recovery-${colorScheme}-native.png`,
  )
  if (await nativeScreenshotHasText(initial, 'Restore pages?')) {
    await fs.rename(initial, recovery)
    return [recovery]
  }
  const permission = path.join(
    screenshotsDir,
    `crash-report-permission-${colorScheme}-native.png`,
  )
  await fs.rename(initial, permission)
  await clickNativeText(permission, 'Block', session.process.child.pid)
  let recoveryReady = false
  for (let attempt = 0; attempt < 20; attempt += 1) {
    await delay(500)
    await captureNativeScreenshot(recovery, session.process.child.pid)
    recoveryReady =
      (await nativeScreenshotHasText(recovery, 'Restore pages?'))
      && (await nativeScreenshotHasText(recovery, '导出诊断信息'))
    if (recoveryReady) break
  }
  if (!recoveryReady) {
    throw new Error('Session recovery bubble lacked diagnostics export')
  }
  return [permission, recovery]
}

async function analyzeVisuals({
  artifacts,
  baselineDir,
  nativeScreenshots,
  pageAudits,
  pageScreenshots,
  runDir,
}) {
  const analyses = []
  const diffDir = path.join(runDir, 'screenshots', 'diff')
  for (const [kind, screenshots] of [
    ['page', pageScreenshots],
    ['native', nativeScreenshots],
  ]) {
    for (const actual of screenshots) {
      analyses.push(
        await analyzeScreenshot({
          actual,
          baselineDir,
          baselineKey: path.join(
            'crash-diagnostics',
            kind,
            path.basename(actual),
          ),
          checkRed: true,
          diffDir,
        }),
      )
    }
  }
  const pageFailures = pageAudits.filter(
    (audit) =>
      audit.contrastFailures.length > 0
      || audit.componentContrastFailures.length > 0
      || audit.layoutFailures.length > 0,
  )
  const imageFailures = analyses.filter((analysis) => !analysis.pass)
  const review = await writeVisualReviewBundle({
    analyses,
    artifacts,
    runDir,
  })
  return {
    analyses,
    imageFailures,
    pageAudits,
    pageFailures,
    review,
    status:
      imageFailures.length === 0 && pageFailures.length === 0 ? 'PASS' : 'FAIL',
  }
}

async function writeReport(runDir, report) {
  await fs.writeFile(
    path.join(runDir, 'report.json'),
    `${JSON.stringify(report, null, 2)}\n`,
  )
  const lines = [
    '# Crash diagnostics QA',
    '',
    `- Status: ${report.status}`,
    `- App: ${report.artifacts?.app || 'not verified'}`,
    `- Bundle: ${report.bundle || 'not created'}`,
    `- Crash reports: ${report.crashes.length}`,
    `- Screenshots: ${report.screenshots.length}`,
    `- Visual checks: ${report.visual?.status || 'not run'}`,
    '',
  ]
  await fs.writeFile(path.join(runDir, 'report.md'), lines.join('\n'))
}

async function main() {
  const config = parseArgs(['--mode', 'smoke', ...process.argv.slice(2)])
  if (config.help) {
    console.log(usage())
    return
  }
  process.env.FP_QA_ALLOW_NATIVE_FOCUS = config.allowNativeFocus ? '1' : '0'
  const runId = `${safeRunId()}-${process.pid}`
  const runDir = path.join(config.resultsDir, `crash-diagnostics-${runId}`)
  const logDir = path.join(runDir, 'logs')
  const screenshotsDir = path.join(runDir, 'screenshots')
  const pageScreenshotsDir = path.join(screenshotsDir, 'page')
  const nativeScreenshotsDir = path.join(screenshotsDir, 'native')
  const outputDir = path.join(runDir, 'export')
  const crashEvidenceDir = path.join(runDir, 'crashes')
  const profilePath = `/tmp/fingerprint-browser-${runId}`
  const crashpadDir = path.join(profilePath, 'Crashpad')
  await Promise.all([
    fs.mkdir(logDir, { recursive: true }),
    fs.mkdir(crashEvidenceDir, { recursive: true }),
    fs.mkdir(pageScreenshotsDir, { recursive: true }),
    fs.mkdir(nativeScreenshotsDir, { recursive: true }),
    fs.mkdir(outputDir, { recursive: true }),
  ])

  const report = {
    artifacts: null,
    bundle: null,
    crashes: [],
    runId,
    screenshots: [],
    status: 'FAIL',
  }
  let session = null
  let nativeSessionPid = null
  try {
    report.artifacts = await prepareAndVerifyArtifacts(config, async () => {})
    if (!config.allowNativeFocus) {
      throw Object.assign(
        new Error(
          'Crash diagnostics native QA requires --allow-native-focus; background mode never opens macOS dialogs',
        ),
        { blocked: true },
      )
    }
    const sessionOptions = {
      app: config.app,
      env: { BREAKPAD_DUMP_LOCATION: crashpadDir },
      extraArgs: ['--enable-crash-reporter-for-testing', '--noerrdialogs'],
      logDir,
      profilePath,
    }
    session = await startQaSession({ ...sessionOptions, name: 'crash-child' })
    report.crashes.push(
      ...(await induceChildCrash(session, 'renderer', crashpadDir)),
    )
    report.crashes.push(
      ...(await induceChildCrash(session, 'gpu', crashpadDir)),
    )
    const browserCrash = await induceBrowserCrash(session, crashpadDir)
    report.crashes.push(...browserCrash.reports)
    await session.close().catch(() => {})
    session = await startQaSession({
      ...sessionOptions,
      background: false,
      name: 'crash-relaunch-light',
      nativeIdleSeconds: config.nativeIdleSeconds,
      profilePreferences: {
        brave: { dark_mode_migrated: true },
        browser: { theme: { color_scheme2: 1 } },
        profile: { exit_type: 'Crashed' },
        selectfile: { last_directory: outputDir },
      },
    })
    await beginNativeUiSession(session.process.child.pid, {
      minimumIdleSeconds: config.nativeIdleSeconds,
    })
    nativeSessionPid = session.process.child.pid
    const recoveryScreenshots = await captureRecoveryUi(
      session,
      nativeScreenshotsDir,
      'light',
    )
    report.screenshots.push(...recoveryScreenshots)
    const pageMatrix = await capturePageMatrix(session, pageScreenshotsDir)
    report.screenshots.push(...pageMatrix.screenshots)
    const helpScreenshots = await captureHelpMenu(
      session,
      nativeScreenshotsDir,
      'light',
    )
    report.screenshots.push(...helpScreenshots)
    const lightTransition = await endNativeUiSession(nativeSessionPid)
    nativeSessionPid = null
    if (!lightTransition.focusRetained) {
      throw Object.assign(
        new Error('Native UI paused because the user changed focus'),
        { status: 'BLOCKED' },
      )
    }
    const darkBrowserCrash = await induceBrowserCrash(session, crashpadDir)
    report.crashes.push(...darkBrowserCrash.reports)
    await session.close().catch(() => {})
    session = await startQaSession({
      ...sessionOptions,
      background: false,
      name: 'crash-relaunch-dark',
      nativeIdleSeconds: 0,
      profilePreferences: {
        brave: { dark_mode_migrated: true },
        browser: { theme: { color_scheme2: 2 } },
        profile: { exit_type: 'Crashed' },
        selectfile: { last_directory: outputDir },
      },
    })
    await beginNativeUiSession(session.process.child.pid, {
      minimumIdleSeconds: 0,
    })
    nativeSessionPid = session.process.child.pid
    report.screenshots.push(
      ...(await captureRecoveryUi(session, nativeScreenshotsDir, 'dark')),
    )
    report.screenshots.push(
      ...(await captureHelpMenu(session, nativeScreenshotsDir, 'dark')),
    )
    report.bundle = await saveDiagnosticsBundle(
      session,
      outputDir,
      nativeScreenshotsDir,
      report.screenshots,
    )
    report.bundleInspection = await inspectDiagnosticsBundle(report.bundle, {
      expectedScope: 'latest_incident',
      forbiddenValues: [
        process.env.FP_QA_DIAGNOSTICS_SECRET_CANARY || '',
      ].filter(Boolean),
    })
    if (report.bundleInspection.crashCount < 3) {
      throw new Error(
        `Expected browser, Renderer, and GPU dumps; found ${report.bundleInspection.crashCount}`,
      )
    }
    report.crashArtifacts = await copyCrashReports(
      report.crashes,
      crashEvidenceDir,
    )
    const nativeScreenshots = report.screenshots.filter((screenshot) =>
      screenshot.startsWith(nativeScreenshotsDir),
    )
    report.visual = await analyzeVisuals({
      artifacts: report.artifacts,
      baselineDir: config.baselineDir,
      nativeScreenshots,
      pageAudits: pageMatrix.audits,
      pageScreenshots: pageMatrix.screenshots,
      runDir,
    })
    if (report.visual.status !== 'PASS') {
      throw Object.assign(new Error('Automated visual checks failed'), {
        details: {
          imageFailures: report.visual.imageFailures,
          pageFailures: report.visual.pageFailures,
        },
      })
    }
    report.status = 'PASS'
  } catch (error) {
    report.error = {
      details: error.details || null,
      message: error.message,
      stack: error.stack,
    }
    report.status =
      error.blocked || error.status === 'BLOCKED' ? 'BLOCKED' : 'FAIL'
    process.exitCode = report.status === 'BLOCKED' ? 2 : 1
  } finally {
    if (nativeSessionPid) {
      await endNativeUiSession(nativeSessionPid).catch(() => {})
    }
    await session?.close().catch(() => {})
    await stopProfileProcesses(profilePath).catch(() => {})
    if (report.crashes.length > 0 && !report.crashArtifacts) {
      report.crashArtifacts = await copyCrashReports(
        report.crashes,
        crashEvidenceDir,
      ).catch(() => [])
    }
    if (!config.keepProfile) {
      await fs.rm(profilePath, { recursive: true, force: true })
    }
    await writeReport(runDir, report)
    console.log(`${report.status}: ${path.join(runDir, 'report.md')}`)
  }
}

await main()
