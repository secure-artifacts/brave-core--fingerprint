// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.
import assert from 'node:assert/strict'
import fs from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'
import test from 'node:test'

import { latestTestSource, runTestBinary } from '../scenarios/code_tests.mjs'

test('runTestBinary rejects a filter that matches no tests', async (t) => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'fingerprint-qa-tests-'))
  t.after(() => fs.rm(dir, { force: true, recursive: true }))
  const binary = path.join(dir, 'fake-test')
  const logFile = path.join(dir, 'test.log')
  await fs.writeFile(
    binary,
    '#!/bin/sh\necho "WARNING: No matching tests to run."\n',
  )
  await fs.chmod(binary, 0o700)

  await assert.rejects(
    runTestBinary({
      binary,
      filter: 'MissingTest.*',
      logFile,
      source: { file: null, mtimeMs: 0 },
      timeoutMs: 1000,
    }),
    /matched no tests/,
  )
})

test('runTestBinary forwards explicit test-only switches', async (t) => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'fingerprint-qa-tests-'))
  t.after(() => fs.rm(dir, { force: true, recursive: true }))
  const binary = path.join(dir, 'fake-test')
  const argsFile = path.join(dir, 'args.txt')
  const logFile = path.join(dir, 'test.log')
  await fs.writeFile(
    binary,
    `#!/bin/sh\nprintf '%s\\n' "$@" > "${argsFile}"\necho '[1/1] Example.Pass'\n`,
  )
  await fs.chmod(binary, 0o700)

  const result = await runTestBinary({
    binary,
    extraArgs: ['--disable-fingerprint-browser-persona-for-testing'],
    filter: 'Example.*',
    logFile,
    source: { file: null, mtimeMs: 0 },
    timeoutMs: 1000,
  })

  assert.deepEqual(result.extraArgs, [
    '--disable-fingerprint-browser-persona-for-testing',
  ])
  assert.match(
    await fs.readFile(argsFile, 'utf8'),
    /--disable-fingerprint-browser-persona-for-testing/,
  )
})

test('latestTestSource ignores newer runtime resources', async (t) => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'fp-qa-code-source-'))
  t.after(() => fs.rm(root, { force: true, recursive: true }))

  const sourceDir = path.join(root, 'component')
  await fs.mkdir(sourceDir)
  const nativeSource = path.join(sourceDir, 'feature.cc')
  const runtimeResource = path.join(sourceDir, 'feature.html')
  await fs.writeFile(nativeSource, 'void Feature() {}\n')
  await fs.writeFile(runtimeResource, '<!doctype html>\n')
  await fs.utimes(nativeSource, new Date(1000), new Date(1000))
  await fs.utimes(runtimeResource, new Date(2000), new Date(2000))

  const latest = await latestTestSource(root, ['component'])
  assert.equal(latest.file, nativeSource)
})
