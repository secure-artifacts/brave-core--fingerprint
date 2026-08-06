// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.
import assert from 'node:assert/strict'
import fs from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'
import test from 'node:test'

import {
  assertNativeUiFocusRetained,
  assertNativeUiIdle,
  copyCrashReports,
  nativeUiFocusAllowed,
  newCrashReports,
  nativeTypeText,
  pngDimensions,
  processesForProfile,
  run,
  snapshotCrashReports,
} from '../lib/system.mjs'
import { contrastRatio } from '../lib/visual.mjs'

test('processesForProfile matches only the exact QA profile', () => {
  const profile = '/tmp/fingerprint-browser-run'
  const processes = [
    {
      command: `指纹浏览器 --user-data-dir=${profile}`,
      pid: 1,
      ppid: 0,
    },
    {
      command: 'Brave Browser --user-data-dir=/Users/test/BraveSoftware',
      pid: 2,
      ppid: 0,
    },
    {
      command: '指纹浏览器 --user-data-dir=/tmp/other',
      pid: 3,
      ppid: 0,
    },
    {
      command: '指纹浏览器 Helper --type=renderer',
      pid: 4,
      ppid: 1,
    },
    {
      command: '指纹浏览器 Helper --type=utility',
      pid: 5,
      ppid: 4,
    },
    {
      command: `指纹浏览器 --user-data-dir=${profile}-other`,
      pid: 6,
      ppid: 0,
    },
  ]
  assert.deepEqual(
    processesForProfile(processes, profile).map((item) => item.pid),
    [1, 4, 5],
  )
})

test('newCrashReports detects new and changed reports', () => {
  const before = [{ file: '/tmp/a.ips', mtimeMs: 1, size: 10 }]
  const after = [
    { file: '/tmp/a.ips', mtimeMs: 2, size: 10 },
    { file: '/tmp/b.ips', mtimeMs: 1, size: 10 },
  ]
  assert.deepEqual(newCrashReports(before, after), after)
})

test('snapshotCrashReports includes Crashpad pending and completed dumps', async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'fp-qa-crashes-'))
  try {
    const native = path.join(directory, 'DiagnosticReports')
    const crashpad = path.join(directory, 'Crashpad')
    await fs.mkdir(native, { recursive: true })
    await fs.mkdir(path.join(crashpad, 'pending'), { recursive: true })
    await fs.mkdir(path.join(crashpad, 'completed'), { recursive: true })
    await fs.writeFile(
      path.join(native, '指纹浏览器-2026-08-02.ips'),
      'ips',
    )
    await fs.writeFile(path.join(native, 'Safari-2026-08-02.ips'), 'ips')
    await fs.writeFile(path.join(crashpad, 'pending', 'pending.dmp'), 'dmp')
    await fs.writeFile(
      path.join(crashpad, 'completed', 'completed.dmp'),
      'dmp',
    )

    const reports = await snapshotCrashReports({
      crashpadDirectories: [crashpad],
      diagnosticReportsDirectory: native,
    })
    assert.deepEqual(
      reports
        .map(({ source, stage }) => `${source}:${stage}`)
        .sort(),
      ['crashpad:completed', 'crashpad:pending', 'native:'],
    )

    const copied = await copyCrashReports(reports, path.join(directory, 'out'))
    assert.equal(copied.length, 3)
    for (const file of copied) await fs.access(file)
  } finally {
    await fs.rm(directory, { recursive: true })
  }
})

test('contrastRatio implements WCAG luminance ratio', () => {
  assert.equal(contrastRatio('rgb(0, 0, 0)', 'rgb(255, 255, 255)'), 21)
  assert.equal(
    contrastRatio('color(srgb 0 0 0 / 1)', 'color(srgb 1 1 1 / 1)'),
    21,
  )
  assert.equal(contrastRatio('transparent', 'rgb(255, 255, 255)'), null)
})

test('run terminates timed out commands', async () => {
  const result = await run('/bin/sleep', ['2'], { timeoutMs: 20 })
  assert.equal(result.timedOut, true)
})

test('nativeTypeText rejects null bytes before invoking macOS automation', async () => {
  await assert.rejects(nativeTypeText('unsafe\0text', 1), /null bytes/)
})

test('native UI input is disabled unless explicitly authorized', async () => {
  assert.equal(nativeUiFocusAllowed({}), false)
  assert.equal(nativeUiFocusAllowed({ FP_QA_ALLOW_NATIVE_FOCUS: '1' }), true)
  await assert.rejects(
    nativeTypeText('safe text', 1, {}),
    /native UI focus is disabled/,
  )
  await assert.rejects(
    assertNativeUiIdle({ env: {} }),
    /native UI focus is disabled/,
  )
  await assert.rejects(
    assertNativeUiFocusRetained(1, {}),
    /native UI focus is disabled/,
  )
})

test(
  'macOS native idle state is readable before foreground automation',
  { skip: process.platform !== 'darwin' },
  async () => {
    const state = await assertNativeUiIdle({
      env: { FP_QA_ALLOW_NATIVE_FOCUS: '1' },
      minimumIdleSeconds: 0,
    })
    assert.ok(Number.isInteger(state.frontmostPid))
    assert.ok(state.idleSeconds >= 0)
  },
)

test('pngDimensions reads IHDR dimensions and rejects invalid files', async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'fp-qa-png-'))
  try {
    const png = path.join(directory, 'pixel.png')
    const invalid = path.join(directory, 'invalid.png')
    await fs.writeFile(
      png,
      Buffer.from(
        'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=',
        'base64',
      ),
    )
    await fs.writeFile(invalid, 'not-png')
    assert.deepEqual(await pngDimensions(png), { height: 1, width: 1 })
    await assert.rejects(pngDimensions(invalid), /Not a valid PNG/)
  } finally {
    await fs.rm(directory, { recursive: true })
  }
})
