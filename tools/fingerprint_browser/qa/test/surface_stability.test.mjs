// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.
import assert from 'node:assert/strict'
import { createHash } from 'node:crypto'
import fs from 'node:fs/promises'
import test from 'node:test'
import { fileURLToPath } from 'node:url'

import {
  combinedSurfaceHash,
  sha256Hex,
} from '../fixtures/probe/surface-hash.js'
import {
  runSurfaceStability,
  runSurfaceStabilityLifecycle,
} from '../scenarios/surface_stability.mjs'

const HASHES = {
  audio: 'b'.repeat(64),
  audioNoise: 'd'.repeat(64),
  canvas: 'a'.repeat(64),
  canvasMods: 'e'.repeat(64),
  canvasVisualization: 'c'.repeat(64),
}

function combinedHash(canvasHash, audioHash) {
  return createHash('sha256')
    .update(canvasHash)
    .update('\0')
    .update(audioHash)
    .digest('hex')
}

function probe(overrides = {}) {
  const canvasHash = overrides.canvasHash || HASHES.canvas
  const audioHash = overrides.audioHash || HASHES.audio
  return {
    audio: {
      hash: audioHash,
      noiseHash: HASHES.audioNoise,
      repeatStable: true,
      sampleCount: 500,
      trapStable: true,
    },
    canvas: {
      hash: canvasHash,
      height: 240,
      modsHash: HASHES.canvasMods,
      modsSignature: 'rgb:1',
      repeatStable: true,
      visualizationHash: HASHES.canvasVisualization,
      width: 480,
    },
    combinedHash: combinedHash(canvasHash, audioHash),
  }
}

function fakePage(results) {
  let index = 0
  const visits = []
  return {
    async evaluate() {
      return results[Math.min(index - 1, results.length - 1)]
    },
    async goto(url) {
      visits.push(url)
      index += 1
    },
    async waitForFunction() {},
    visits,
  }
}

function emptyEvents() {
  return {
    browserExits: [],
    console: [],
    crashes: [],
    disconnected: [],
    pageErrors: [],
  }
}

test('local surface probe hashes bytes and Canvas plus Audio independently', async () => {
  const bytes = new Uint8Array([0, 1, 2, 253, 254, 255])
  const expectedBytes = createHash('sha256').update(bytes).digest('hex')
  const expectedCombined = combinedHash(HASHES.canvas, HASHES.audio)

  assert.equal(await sha256Hex(bytes), expectedBytes)
  assert.equal(
    await combinedSurfaceHash(HASHES.canvas, HASHES.audio),
    expectedCombined,
  )
})

test('fixture packages a self-contained Canvas and OfflineAudio gate', async () => {
  const fixtureRoot = fileURLToPath(
    new URL('../fixtures/probe/', import.meta.url),
  )
  const [pageBody, scriptBody] = await Promise.all([
    fs.readFile(`${fixtureRoot}/surface-stability.html`, 'utf8'),
    fs.readFile(`${fixtureRoot}/surface-stability.js`, 'utf8'),
  ])

  assert.match(pageBody, /surface-stability\.js/)
  assert.match(pageBody, /canvas-visualization/)
  assert.match(scriptBody, /OfflineAudioContext/)
  assert.match(scriptBody, /visualizationHash/)
  assert.match(scriptBody, /modsHash/)
  assert.match(scriptBody, /noiseHash/)
  assert.doesNotMatch(scriptBody, /https?:\/\//)
})

test('same-Profile surface gate records 20 complete hash iterations', async () => {
  const page = fakePage(Array.from({ length: 20 }, () => probe()))
  const result = await runSurfaceStability({
    copyReports: async () => [],
    crashArtifactsDir: '/tmp/evidence',
    crashpadDirectories: ['/tmp/profile/Crashpad'],
    events: emptyEvents(),
    origin: 'http://127.0.0.1:9000',
    page,
    snapshotReports: async () => [],
  })

  assert.equal(page.visits.length, 20)
  assert.equal(result.iterationCount, 20)
  assert.equal(result.iterations.length, 20)
  assert.deepEqual(result.iterations[0], {
    audioHash: HASHES.audio,
    audioNoiseHash: HASHES.audioNoise,
    canvasHash: HASHES.canvas,
    canvasModsHash: HASHES.canvasMods,
    canvasVisualizationHash: HASHES.canvasVisualization,
    combinedHash: combinedHash(HASHES.canvas, HASHES.audio),
    iteration: 1,
  })
  assert.equal(result.iterations.at(-1).iteration, 20)
})

test('surface lifecycle keeps one Profile through three cold restarts', async () => {
  const record = {
    audioHash: HASHES.audio,
    audioNoiseHash: HASHES.audioNoise,
    canvasHash: HASHES.canvas,
    canvasModsHash: HASHES.canvasMods,
    canvasVisualizationHash: HASHES.canvasVisualization,
    combinedHash: combinedHash(HASHES.canvas, HASHES.audio),
    iteration: 1,
  }
  const launches = []
  const closed = []
  const crashpadDirectories = []
  const result = await runSurfaceStabilityLifecycle({
    config: {
      app: '/tmp/current.app',
      background: true,
      crashpadDir: '/tmp/fingerprint-browser-test-run/Crashpad',
      nativeIdleSeconds: 60,
    },
    dirs: { crashes: '/tmp/crashes', logs: '/tmp/logs' },
    probe: { origin: 'http://127.0.0.1:9000' },
    runId: 'test-run',
    settleAfterCloseMs: 0,
    snapshotReports: async (options) => {
      crashpadDirectories.push(...options.crashpadDirectories)
      return []
    },
    runStability: async ({ iterations }) => ({
      iterationCount: iterations,
      iterations: Array.from({ length: iterations }, (_, index) => ({
        ...record,
        iteration: index + 1,
      })),
      runtimeEvents: emptyEvents(),
      url: 'http://127.0.0.1:9000/surface-stability.html',
    }),
    startSession: async (options) => {
      launches.push(options)
      return {
        async close() {
          closed.push(options.name)
        },
        context: { pages: () => [{}] },
        events: emptyEvents(),
      }
    },
  })

  assert.equal(launches.length, 4)
  assert.equal(closed.length, 4)
  assert.deepEqual(
    new Set(launches.map((item) => item.profilePath)),
    new Set(['/tmp/fingerprint-browser-test-run/surface-stability']),
  )
  assert.deepEqual(
    result.launches.map((item) => item.iterationCount),
    [20, 1, 1, 1],
  )
  assert.equal(result.restartCount, 3)
  assert.ok(
    crashpadDirectories.includes('/tmp/fingerprint-browser-test-run/Crashpad'),
  )
})

test('surface lifecycle retains crash reports written while closing', async () => {
  const snapshots = [
    [],
    [
      {
        file: '/tmp/profile/Crashpad/pending/close.dmp',
        mtimeMs: 2,
        size: 9,
        source: 'crashpad',
        stage: 'pending',
      },
    ],
  ]
  const copied = []

  await assert.rejects(
    runSurfaceStabilityLifecycle({
      config: {
        app: '/tmp/current.app',
        background: true,
        nativeIdleSeconds: 60,
      },
      copyReports: async (reports) => {
        copied.push(...reports)
        return ['/tmp/evidence/close.dmp']
      },
      dirs: { crashes: '/tmp/crashes', logs: '/tmp/logs' },
      probe: { origin: 'http://127.0.0.1:9000' },
      restartCount: 0,
      runId: 'close-crash',
      runStability: async () => ({
        iterationCount: 20,
        iterations: [
          {
            audioHash: HASHES.audio,
            audioNoiseHash: HASHES.audioNoise,
            canvasHash: HASHES.canvas,
            canvasModsHash: HASHES.canvasMods,
            canvasVisualizationHash: HASHES.canvasVisualization,
            combinedHash: combinedHash(HASHES.canvas, HASHES.audio),
            iteration: 1,
          },
        ],
        runtimeEvents: emptyEvents(),
      }),
      settleAfterCloseMs: 0,
      snapshotReports: async () => snapshots.shift() || snapshots.at(-1),
      startSession: async () => ({
        async close() {},
        context: { pages: () => [{}] },
        events: emptyEvents(),
      }),
    }),
    /appeared on close/,
  )
  assert.equal(copied.length, 1)
})

test('same-Profile surface gate rejects every local hash change', async () => {
  const changes = [
    {
      name: 'Canvas',
      update: (value) => probe({ canvasHash: '1'.repeat(64) }),
    },
    {
      name: 'Canvas modifications',
      update: (value) => ({
        ...value,
        canvas: { ...value.canvas, modsHash: '6'.repeat(64) },
      }),
    },
    {
      name: 'Canvas visualization',
      update: (value) => ({
        ...value,
        canvas: { ...value.canvas, visualizationHash: '2'.repeat(64) },
      }),
    },
    {
      name: 'Audio',
      update: (value) => probe({ audioHash: '3'.repeat(64) }),
    },
    {
      name: 'Audio noise',
      update: (value) => ({
        ...value,
        audio: { ...value.audio, noiseHash: '4'.repeat(64) },
      }),
    },
    {
      name: 'combined',
      update: (value) => ({ ...value, combinedHash: '5'.repeat(64) }),
    },
  ]

  for (const change of changes) {
    const first = probe()
    const page = fakePage([first, change.update(first)])
    await assert.rejects(
      runSurfaceStability({
        copyReports: async () => [],
        crashArtifactsDir: '/tmp/evidence',
        crashpadDirectories: ['/tmp/profile/Crashpad'],
        events: emptyEvents(),
        iterations: 2,
        origin: 'http://127.0.0.1:9000',
        page,
        snapshotReports: async () => [],
      }),
      new RegExp(`${change.name} hash changed|combined hash is inconsistent`),
      change.name,
    )
  }
})

test('surface gate rejects Audio trap position drift', async () => {
  const value = probe()
  value.audio.trapStable = false
  await assert.rejects(
    runSurfaceStability({
      copyReports: async () => [],
      crashArtifactsDir: '/tmp/evidence',
      crashpadDirectories: ['/tmp/profile/Crashpad'],
      events: emptyEvents(),
      iterations: 1,
      origin: 'http://127.0.0.1:9000',
      page: fakePage([value]),
      snapshotReports: async () => [],
    }),
    /Equal Audio samples changed across buffer positions/,
  )
})

test('surface gate rejects browser, renderer, CDP, and crash-page events', async () => {
  const cases = [
    {
      expected: /browser exited/,
      update: (events) => events.browserExits.push({ code: 9, signal: null }),
    },
    {
      expected: /renderer crashed/,
      update: (events) => events.crashes.push({ page: 'http://probe/' }),
    },
    {
      expected: /CDP disconnected/,
      update: (events) => events.disconnected.push({}),
    },
    {
      expected: /Aw, Snap/,
      update: (events) =>
        events.console.push({ text: 'Aw, Snap! renderer terminated' }),
    },
  ]

  for (const item of cases) {
    const events = emptyEvents()
    item.update(events)
    await assert.rejects(
      runSurfaceStability({
        copyReports: async () => [],
        crashArtifactsDir: '/tmp/evidence',
        crashpadDirectories: ['/tmp/profile/Crashpad'],
        events,
        iterations: 1,
        origin: 'http://127.0.0.1:9000',
        page: fakePage([probe()]),
        snapshotReports: async () => [],
      }),
      item.expected,
    )
  }
})

test('surface gate rejects and retains each new native or Crashpad report', async () => {
  for (const report of [
    {
      file: '/tmp/DiagnosticReports/Brave-1.ips',
      mtimeMs: 1,
      size: 10,
      source: 'native',
    },
    {
      file: '/tmp/profile/Crashpad/pending/1.dmp',
      mtimeMs: 1,
      size: 10,
      source: 'crashpad',
      stage: 'pending',
    },
  ]) {
    let snapshot = 0
    let copied = null
    await assert.rejects(
      runSurfaceStability({
        copyReports: async (reports) => {
          copied = reports
          return ['/tmp/evidence/' + report.file.split('/').at(-1)]
        },
        crashArtifactsDir: '/tmp/evidence',
        crashpadDirectories: ['/tmp/profile/Crashpad'],
        events: emptyEvents(),
        iterations: 1,
        origin: 'http://127.0.0.1:9000',
        page: fakePage([probe()]),
        snapshotReports: async () => (snapshot++ === 0 ? [] : [report]),
      }),
      /new crash report/,
    )
    assert.deepEqual(copied, [report])
  }
})
