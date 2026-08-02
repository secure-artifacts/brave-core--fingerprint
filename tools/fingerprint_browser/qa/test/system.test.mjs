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
  newCrashReports,
  pngDimensions,
  processesForProfile,
  run,
} from '../lib/system.mjs'
import { contrastRatio } from '../lib/visual.mjs'

test('processesForProfile matches only Development QA command line', () => {
  const profile = '/tmp/fingerprint-browser-run'
  const processes = [
    {
      command: `Brave Browser Development --user-data-dir=${profile}`,
      pid: 1,
      ppid: 0,
    },
    {
      command: 'Brave Browser --user-data-dir=/Users/test/BraveSoftware',
      pid: 2,
      ppid: 0,
    },
    {
      command: 'Brave Browser Development --user-data-dir=/tmp/other',
      pid: 3,
      ppid: 0,
    },
    {
      command: 'Brave Browser Development Helper --type=renderer',
      pid: 4,
      ppid: 1,
    },
    {
      command: 'Brave Browser Development Helper --type=utility',
      pid: 5,
      ppid: 4,
    },
    {
      command: `Brave Browser Development --user-data-dir=${profile}-other`,
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
