// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.
import assert from 'node:assert/strict'
import fs from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'
import test from 'node:test'

import { finalStatus, runScenario, writeReports } from '../lib/report.mjs'

test('finalStatus prioritizes failures then blockers', () => {
  assert.equal(finalStatus([{ status: 'PASS' }]), 'PASS')
  assert.equal(
    finalStatus([{ status: 'PASS' }, { status: 'BLOCKED' }]),
    'BLOCKED',
  )
  assert.equal(finalStatus([{ status: 'BLOCKED' }, { status: 'FAIL' }]), 'FAIL')
})

test('runScenario captures failures without aborting the run', async () => {
  const report = { scenarios: [] }
  const scenario = await runScenario(report, 'failure', async () => {
    throw new Error('expected failure')
  })
  assert.equal(scenario.status, 'FAIL')
  assert.match(scenario.reason, /expected failure/)
})

test('runScenario does not let evidence text replace a gate status', async () => {
  const report = { scenarios: [] }
  const scenario = await runScenario(report, 'evidence-status', async () => ({
    status: '9 of 9 values match',
  }))

  assert.equal(scenario.status, 'PASS')
  assert.equal(scenario.resultStatus, '9 of 9 values match')
})

test('writeReports redacts secrets and writes all formats', async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'fp-qa-report-'))
  const report = {
    config: { app: '/tmp/App.app', password: 'secret' },
    mode: 'smoke',
    profileRoot: '/tmp/profile',
    runId: 'run',
    scenarios: [{ id: 'pass', status: 'PASS' }],
    startedAt: new Date(0).toISOString(),
  }
  try {
    await fs.mkdir(path.join(directory, 'logs'))
    await fs.mkdir(path.join(directory, 'crashes'))
    await fs.mkdir(path.join(directory, 'screenshots'))
    await writeReports(report, directory)
    const json = await fs.readFile(path.join(directory, 'report.json'), 'utf8')
    assert.doesNotMatch(json, /secret/)
    assert.match(json, /<redacted>/)
    assert.match(
      await fs.readFile(path.join(directory, 'junit.xml'), 'utf8'),
      /testsuite/,
    )
    assert.match(
      await fs.readFile(path.join(directory, 'report.md'), 'utf8'),
      /PASS/,
    )
  } finally {
    await fs.rm(directory, { recursive: true, force: true })
  }
})
