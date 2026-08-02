// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

import fs from 'node:fs/promises'
import path from 'node:path'

import { parseArgs, usage } from './lib/args.mjs'
import { prepareAndVerifyArtifacts } from './lib/artifacts.mjs'
import { startQaSession } from './lib/browser.mjs'
import { startProbeServer } from './lib/probe_server.mjs'
import { runScenario, writeReports } from './lib/report.mjs'
import {
  copyCrashReports,
  listProcesses,
  newCrashReports,
  safeRunId,
  scanFatalLogs,
  snapshotCrashReports,
  stopAllQaProcesses,
} from './lib/system.mjs'
import {
  runBrowserTests,
  runNetTests,
  runPerformanceManagerTests,
  runUnitTests,
} from './scenarios/code_tests.mjs'
import { runFull, runProxyFixtures } from './scenarios/full.mjs'
import { runSmoke } from './scenarios/smoke.mjs'
import { runSoak } from './scenarios/soak.mjs'
import {
  analyzeScreenshotTree,
  loadHumanVisualReview,
  writeVisualReviewBundle,
} from './lib/visual.mjs'

async function makeDirectories(runDir) {
  const dirs = {
    crashes: path.join(runDir, 'crashes'),
    diff: path.join(runDir, 'screenshots', 'diff'),
    logs: path.join(runDir, 'logs'),
    native: path.join(runDir, 'screenshots', 'native'),
    page: path.join(runDir, 'screenshots', 'page'),
  }
  await Promise.all(
    Object.values(dirs).map((directory) =>
      fs.mkdir(directory, { recursive: true }),
    ),
  )
  return dirs
}

async function main() {
  let config
  try {
    config = parseArgs(process.argv.slice(2))
  } catch (error) {
    console.error(error.message)
    console.error(usage())
    process.exitCode = 1
    return
  }
  if (config.help) {
    console.log(usage())
    return
  }

  const runId = `${safeRunId()}-${process.pid}`
  const runDir = path.join(config.resultsDir, runId)
  const dirs = await makeDirectories(runDir)
  const profileRoot = `/tmp/fingerprint-browser-${runId}`
  const logFile = path.join(dirs.logs, 'runner.log')
  const log = async (message) => {
    const line = `${new Date().toISOString()} ${message}`
    console.log(line)
    await fs.appendFile(logFile, `${line}\n`)
  }
  const report = {
    artifacts: null,
    config: {
      app: config.app,
      baselineDir: config.baselineDir,
      durationMinutes: config.durationMinutes,
      prepareApp: config.prepareApp,
      proxyFixtures: config.proxyFixtures,
      skipCodeTests: config.skipCodeTests,
    },
    mode: config.mode,
    profileRoot,
    runId,
    scenarios: [],
    startedAt: new Date().toISOString(),
  }
  config.profileRoot = profileRoot

  let probe = null
  let smokeSession = null
  let crashesBefore = []
  let artifactGatePassed = false
  try {
    const processGate = await runScenario(
      report,
      'preflight-stop-old-qa-processes',
      async () => {
        const stopped = await stopAllQaProcesses()
        await log(
          `Stopped ${stopped.length} previous QA profile process groups`,
        )
        const residual = stopped.flatMap((result) => result.residual || [])
        if (residual.length > 0) {
          throw Object.assign(
            new Error(
              `${residual.length} old QA processes could not be stopped`,
            ),
            {
              details: residual,
            },
          )
        }
        return { stopped }
      },
    )
    if (processGate.status !== 'PASS') {
      await writeReports(report, runDir)
      await log('QA process cleanup gate failed; browser launch blocked')
      return
    }

    if (config.keepProfile && config.mode !== 'smoke') {
      await runScenario(
        report,
        'preflight-profile-cleanup-required',
        async () => ({
          reason: 'Full/Soak delivery cannot use --keep-profile',
          status: 'BLOCKED',
        }),
      )
    }

    const artifactScenario = await runScenario(
      report,
      'preflight-current-artifacts',
      async () => {
        report.artifacts = await prepareAndVerifyArtifacts(config, log)
        return report.artifacts
      },
    )
    artifactGatePassed = artifactScenario.status === 'PASS'
    await writeReports(report, runDir)
    if (!artifactGatePassed) {
      await log('Artifact gate failed; browser launch blocked')
      return
    }

    crashesBefore = await snapshotCrashReports()
    report.crashesBefore = crashesBefore

    if (config.mode === 'full' || config.mode === 'soak') {
      const codeGates = []
      if (config.skipCodeTests) {
        codeGates.push(
          await runScenario(report, 'code-tests-required', async () => ({
            reason: 'Full/Soak cannot be delivered with --skip-code-tests',
            status: 'BLOCKED',
          })),
        )
      } else {
        codeGates.push(
          await runScenario(
            report,
            'code-tests-unit',
            async () =>
              await runUnitTests({
                braveRoot: config.braveRoot,
                dirs,
                outDir: config.outDir,
              }),
          ),
          await runScenario(
            report,
            'code-tests-net',
            async () =>
              await runNetTests({
                braveRoot: config.braveRoot,
                dirs,
                outDir: config.outDir,
              }),
          ),
          await runScenario(
            report,
            'code-tests-performance-manager',
            async () =>
              await runPerformanceManagerTests({
                braveRoot: config.braveRoot,
                dirs,
                outDir: config.outDir,
              }),
          ),
          await runScenario(
            report,
            'code-tests-browser',
            async () =>
              await runBrowserTests({
                braveRoot: config.braveRoot,
                dirs,
                outDir: config.outDir,
              }),
          ),
        )
      }
      if (codeGates.some((scenario) => scenario.status !== 'PASS')) {
        await writeReports(report, runDir)
        await log('C++ test gate failed or blocked; browser launch blocked')
        return
      }
    }

    probe = await startProbeServer(
      path.join(
        config.braveRoot,
        'tools',
        'fingerprint_browser',
        'qa',
        'fixtures',
        'probe',
      ),
    )
    smokeSession = await startQaSession({
      app: config.app,
      logDir: dirs.logs,
      name: 'smoke',
      profilePath: path.join(profileRoot, 'smoke'),
    })
    const smokeScenarioIndex = report.scenarios.length
    await runSmoke({ config, dirs, probe, report, session: smokeSession })
    await smokeSession.close()
    smokeSession = null
    await writeReports(report, runDir)
    if (
      report.scenarios
        .slice(smokeScenarioIndex)
        .some((scenario) => scenario.status !== 'PASS')
    ) {
      await log('Smoke gate failed; Full/Soak launch blocked')
      return
    }

    let fullPassed = true
    if (config.mode === 'proxy') {
      const proxyScenarioIndex = report.scenarios.length
      await runProxyFixtures({ config, dirs, probe, report, runId })
      fullPassed = report.scenarios
        .slice(proxyScenarioIndex)
        .every((scenario) => scenario.status === 'PASS')
      await writeReports(report, runDir)
    } else if (config.mode === 'full' || config.mode === 'soak') {
      const fullScenarioIndex = report.scenarios.length
      await runFull({ config, dirs, probe, report, runId })
      fullPassed = report.scenarios
        .slice(fullScenarioIndex)
        .every((scenario) => scenario.status === 'PASS')
      await writeReports(report, runDir)
    }
    if (config.mode === 'soak' && fullPassed) {
      await runSoak({ config, dirs, probe, report, runId })
    }
    await runScenario(report, 'visual-analysis-all-screenshots', async () => {
      const result = await analyzeScreenshotTree({
        baselineDir: config.baselineDir,
        diffDir: dirs.diff,
        nativeDir: dirs.native,
        pageDir: dirs.page,
        requireNativeBaselines:
          config.mode === 'full' || config.mode === 'soak',
      })
      if (result.analyses.length === 0) {
        throw new Error('No screenshots were captured')
      }
      const reviewBundle = await writeVisualReviewBundle({
        analyses: result.analyses,
        artifacts: report.artifacts,
        runDir,
      })
      const screenshots = result.analyses.map((analysis) => analysis.actual)
      if (result.failed.length > 0) {
        return {
          analyses: result.analyses,
          reason: `${result.failed.length} screenshots failed visual analysis`,
          reviewBundle,
          screenshots,
          status: 'FAIL',
        }
      }
      if (result.missingRequiredBaselines.length > 0) {
        return {
          analyses: result.analyses,
          reason: `${result.missingRequiredBaselines.length} baselines require manual approval`,
          reviewBundle,
          screenshots,
          status: 'BLOCKED',
        }
      }
      if (config.mode === 'full' || config.mode === 'soak') {
        const review = await loadHumanVisualReview({
          analyses: result.analyses,
          artifacts: report.artifacts,
          manifestFile: process.env.FP_QA_VISUAL_REVIEW_MANIFEST,
        })
        if (review.status !== 'PASS') {
          return {
            analyses: result.analyses,
            review,
            reviewBundle,
            screenshots,
            status: review.status,
          }
        }
        return { analyses: result.analyses, review, reviewBundle, screenshots }
      }
      return { analyses: result.analyses, reviewBundle, screenshots }
    })
  } catch (error) {
    await runScenario(report, 'runner-unhandled-error', async () => {
      throw error
    })
  } finally {
    if (smokeSession) {
      await smokeSession.close().catch(() => {})
    }
    if (probe) {
      await probe.close().catch(() => {})
    }

    await runScenario(report, 'postflight-cleanup', async () => {
      const stopped = await stopAllQaProcesses()
      const residual = [
        ...stopped.flatMap((result) => result.residual || []),
        ...(await listProcesses()).filter(
          (item) =>
            item.command.includes('Brave Browser Development')
            && item.command.includes('/tmp/fingerprint-browser-'),
        ),
      ]
      if (residual.length > 0) {
        throw Object.assign(
          new Error(`${residual.length} QA processes remained`),
          {
            details: residual,
          },
        )
      }
      if (!config.keepProfile) {
        await fs.rm(profileRoot, { recursive: true, force: true })
      }
      return { keptProfile: config.keepProfile, residual: [] }
    })

    if (artifactGatePassed) {
      await new Promise((resolve) => setTimeout(resolve, 5000))
      const crashesAfter = await snapshotCrashReports()
      const crashes = newCrashReports(crashesBefore, crashesAfter)
      const copied = await copyCrashReports(crashes, dirs.crashes)
      await runScenario(report, 'postflight-no-new-crashes', async () => {
        if (crashes.length > 0) {
          const error = Object.assign(
            new Error(`${crashes.length} new macOS crash reports found`),
            { details: { crashes, copied, crashArtifacts: copied } },
          )
          error.crashArtifacts = copied
          throw error
        }
        return { crashArtifacts: [], crashes: [], copied: [] }
      })
      await runScenario(
        report,
        'postflight-no-fatal-browser-logs',
        async () => {
          const failures = await scanFatalLogs(dirs.logs)
          if (failures.length > 0) {
            throw Object.assign(
              new Error(`${failures.length} fatal browser log entries found`),
              {
                details: failures,
              },
            )
          }
          return { failures }
        },
      )
    }

    const finalReport = await writeReports(report, runDir)
    await log(`QA ${config.mode} finished: ${finalReport.status}`)
    await log(`Report: ${path.join(runDir, 'report.md')}`)
    process.exitCode =
      finalReport.status === 'PASS'
        ? 0
        : finalReport.status === 'BLOCKED'
          ? 2
          : 1
  }
}

await main()
