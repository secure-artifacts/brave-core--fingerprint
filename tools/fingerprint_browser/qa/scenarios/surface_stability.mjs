// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.
import { createHash } from 'node:crypto'
import path from 'node:path'

import { startQaSession } from '../lib/browser.mjs'
import {
  copyCrashReports,
  newCrashReports,
  snapshotCrashReports,
} from '../lib/system.mjs'

const SHA256 = /^[0-9a-f]{64}$/
const STABLE_HASHES = [
  ['canvasHash', 'Canvas'],
  ['canvasModsHash', 'Canvas modifications'],
  ['canvasVisualizationHash', 'Canvas visualization'],
  ['audioHash', 'Audio'],
  ['audioNoiseHash', 'Audio noise'],
  ['combinedHash', 'combined'],
]

function expectedCombinedHash(canvasHash, audioHash) {
  return createHash('sha256')
    .update(canvasHash)
    .update('\0')
    .update(audioHash)
    .digest('hex')
}

function requireHash(value, name) {
  if (!SHA256.test(value || '')) {
    throw new Error(`Surface probe omitted valid ${name}`)
  }
  return value
}

function iterationRecord(result, iteration) {
  const canvasHash = requireHash(result?.canvas?.hash, 'Canvas hash')
  const canvasVisualizationHash = requireHash(
    result?.canvas?.visualizationHash,
    'Canvas visualization hash',
  )
  const canvasModsHash = requireHash(
    result?.canvas?.modsHash,
    'Canvas modifications hash',
  )
  const audioHash = requireHash(result?.audio?.hash, 'Audio hash')
  const audioNoiseHash = requireHash(
    result?.audio?.noiseHash,
    'Audio noise hash',
  )
  const combinedHash = requireHash(result?.combinedHash, 'combined hash')
  const expected = expectedCombinedHash(canvasHash, audioHash)
  if (combinedHash !== expected) {
    throw new Error('Surface probe combined hash is inconsistent')
  }
  if (result.canvas.repeatStable !== true) {
    throw new Error('Canvas repeated pixel readback changed within probe')
  }
  if (result.audio.repeatStable !== true) {
    throw new Error('Audio repeated readback changed within probe')
  }
  if (result.audio.trapStable !== true) {
    throw new Error('Equal Audio samples changed across buffer positions')
  }
  return {
    audioHash,
    audioNoiseHash,
    canvasHash,
    canvasModsHash,
    canvasVisualizationHash,
    combinedHash,
    iteration,
  }
}

function runtimeFailures(events) {
  return [
    ...events.browserExits.map(
      (event) => `browser exited ${event.code ?? event.signal ?? 'unknown'}`,
    ),
    ...events.crashes.map(
      (event) => `renderer crashed ${event.page || 'unknown page'}`,
    ),
    ...events.disconnected.map(() => 'CDP disconnected'),
    ...events.pageErrors.map((event) => `page error ${event.message}`),
    ...events.console
      .filter((event) =>
        /CHECK failed|FATAL|DYLD|Aw, Snap|crash/i.test(event.text),
      )
      .map((event) => `fatal console ${event.text}`),
  ]
}

async function healthError({
  copyReports,
  crashArtifactsDir,
  crashesBefore,
  crashpadDirectories,
  events,
  snapshotReports,
}) {
  const failures = runtimeFailures(events)
  const crashesAfter = await snapshotReports({ crashpadDirectories })
  const crashes = newCrashReports(crashesBefore, crashesAfter)
  const crashArtifacts =
    crashes.length > 0 ? await copyReports(crashes, crashArtifactsDir) : []
  if (failures.length === 0 && crashes.length === 0) return null
  const reasons = [...failures]
  if (crashes.length > 0) {
    reasons.push(
      `${crashes.length} new crash report${crashes.length === 1 ? '' : 's'} found`,
    )
  }
  const error = Object.assign(new Error(reasons.join('; ')), {
    crashArtifacts,
    details: { crashArtifacts, crashes, runtimeEvents: events },
  })
  return error
}

export async function runSurfaceStability({
  copyReports = copyCrashReports,
  crashArtifactsDir,
  crashpadDirectories = [],
  events,
  iterations = 20,
  origin,
  page,
  snapshotReports = snapshotCrashReports,
}) {
  const records = []
  const crashesBefore = await snapshotReports({ crashpadDirectories })
  for (let iteration = 1; iteration <= iterations; iteration += 1) {
    try {
      await page.goto(
        `${origin}/surface-stability.html?iteration=${iteration}`,
        { waitUntil: 'load' },
      )
      await page.waitForFunction(
        () =>
          window.__fpSurfaceStabilityReady || window.__fpSurfaceStabilityError,
        null,
        { timeout: 30000 },
      )
      const result = await page.evaluate(() => {
        if (window.__fpSurfaceStabilityError) {
          throw new Error(window.__fpSurfaceStabilityError)
        }
        return window.__fpSurfaceStabilityResult
      })
      const record = iterationRecord(result, iteration)
      const baseline = records[0]
      if (baseline) {
        for (const [property, label] of STABLE_HASHES) {
          if (record[property] !== baseline[property]) {
            throw Object.assign(
              new Error(`${label} hash changed at iteration ${iteration}`),
              {
                details: {
                  baseline: baseline[property],
                  iteration,
                  observed: record[property],
                  property,
                },
              },
            )
          }
        }
      }
      records.push(record)
    } catch (error) {
      const failure = await healthError({
        copyReports,
        crashArtifactsDir,
        crashesBefore,
        crashpadDirectories,
        events,
        snapshotReports,
      })
      if (failure) {
        failure.details.operationError = error.message
        throw failure
      }
      throw error
    }
    const failure = await healthError({
      copyReports,
      crashArtifactsDir,
      crashesBefore,
      crashpadDirectories,
      events,
      snapshotReports,
    })
    if (failure) throw failure
  }
  return {
    iterationCount: records.length,
    iterations: records,
    runtimeEvents: events,
    url: `${origin}/surface-stability.html`,
  }
}

function compareRecord(expected, observed, label) {
  for (const [property, name] of STABLE_HASHES) {
    if (observed[property] !== expected[property]) {
      throw Object.assign(new Error(`${name} hash changed after ${label}`), {
        details: {
          expected: expected[property],
          label,
          observed: observed[property],
          property,
        },
      })
    }
  }
}

export async function runSurfaceStabilityLifecycle({
  config,
  copyReports = copyCrashReports,
  dirs,
  probe,
  restartCount = 3,
  runStability = runSurfaceStability,
  runId,
  settleAfterCloseMs = 1000,
  snapshotReports = snapshotCrashReports,
  startSession = startQaSession,
}) {
  const profilePath = `/tmp/fingerprint-browser-${runId}/surface-stability`
  const crashpadDirectories = [
    ...new Set(
      [config.crashpadDir, path.join(profilePath, 'Crashpad')].filter(Boolean),
    ),
  ]
  const launches = []
  let baseline = null

  for (let launch = 0; launch <= restartCount; launch += 1) {
    const session = await startSession({
      app: config.app,
      background: config.background,
      logDir: dirs.logs,
      name: `surface-stability-${launch}`,
      nativeIdleSeconds: config.nativeIdleSeconds,
      profilePath,
    })
    let operationError = null
    let result = null
    try {
      const page =
        session.context.pages()[0] || (await session.context.newPage())
      result = await runStability({
        crashArtifactsDir: dirs.crashes,
        crashpadDirectories,
        events: session.events,
        iterations: launch === 0 ? 20 : 1,
        origin: probe.origin,
        page,
      })
    } catch (error) {
      operationError = error
    }

    const crashesBeforeClose = await snapshotReports({ crashpadDirectories })
    try {
      await session.close()
    } catch (error) {
      operationError ||= error
    }
    if (settleAfterCloseMs > 0) {
      await new Promise((resolve) => setTimeout(resolve, settleAfterCloseMs))
    }
    const crashesAfterClose = await snapshotReports({ crashpadDirectories })
    const closeCrashes = newCrashReports(crashesBeforeClose, crashesAfterClose)
    if (closeCrashes.length > 0) {
      const crashArtifacts = await copyReports(closeCrashes, dirs.crashes)
      throw Object.assign(
        new Error(`${closeCrashes.length} crash report(s) appeared on close`),
        {
          crashArtifacts,
          details: {
            crashArtifacts,
            crashes: closeCrashes,
            operationError: operationError?.message,
          },
        },
      )
    }
    if (operationError) throw operationError

    const observed = result.iterations[0]
    if (baseline) compareRecord(baseline, observed, `restart ${launch}`)
    else baseline = observed
    launches.push({ launch, ...result })
  }

  return {
    baseline,
    launches,
    profilePath,
    restartCount,
  }
}
