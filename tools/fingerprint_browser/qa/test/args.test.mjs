// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.
import assert from 'node:assert/strict'
import path from 'node:path'
import test from 'node:test'

import { parseArgs } from '../lib/args.mjs'

test('parseArgs derives Brave Component defaults', () => {
  const cwd = '/workspace/src/brave'
  const parsed = parseArgs(['--mode', 'full'], { braveRoot: cwd, cwd })
  assert.equal(parsed.mode, 'full')
  assert.equal(
    parsed.app,
    path.join(
      '/workspace/src/out/Component_arm64',
      'fingerprint-browser-qa',
      'Brave Browser Development QA.app',
    ),
  )
  assert.equal(parsed.prepareApp, true)
})

test('parseArgs rejects unknown modes and invalid soak durations', () => {
  assert.throws(() => parseArgs(['--mode', 'fast']), /Invalid mode/)
  assert.throws(
    () => parseArgs(['--duration-minutes', '0']),
    /must be greater than zero/,
  )
})

test('parseArgs recognizes proxy diagnostics mode', () => {
  assert.equal(parseArgs(['--mode', 'proxy']).mode, 'proxy')
})

test('parseArgs recognizes gate flags', () => {
  const parsed = parseArgs([
    '--no-prepare-app',
    '--skip-code-tests',
    '--keep-profile',
  ])
  assert.equal(parsed.prepareApp, false)
  assert.equal(parsed.skipCodeTests, true)
  assert.equal(parsed.keepProfile, true)
})
