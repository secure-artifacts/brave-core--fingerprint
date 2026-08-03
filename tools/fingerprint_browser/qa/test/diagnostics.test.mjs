// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

import { createHash } from 'node:crypto'
import fs from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'
import test from 'node:test'
import assert from 'node:assert/strict'

import { inspectDiagnosticsBundle } from '../lib/diagnostics.mjs'
import { run } from '../lib/system.mjs'

function digest(contents) {
  return createHash('sha256').update(contents).digest('hex')
}

async function createBundle(root, secret = '') {
  const payloads = new Map([
    ['README.txt', Buffer.from('Local diagnostic bundle\n')],
    ['state/browser.json', Buffer.from('{"ready":true}\n')],
    ['state/extensions.json', Buffer.from('{"items":[]}\n')],
    ['state/fingerprint.json', Buffer.from('{"valid":true}\n')],
    ['state/profiles.json', Buffer.from('{"regular":true}\n')],
    ['state/proxy.json', Buffer.from(`{"state":"active","note":"${secret}"}\n`)],
  ])
  for (const [relative, contents] of payloads) {
    const target = path.join(root, relative)
    await fs.mkdir(path.dirname(target), { recursive: true })
    await fs.writeFile(target, contents)
  }
  const manifest = {
    crashCount: 0,
    files: [...payloads].map(([relative, contents]) => ({
      path: relative,
      sha256: digest(contents),
      sizeBytes: String(contents.length),
    })),
    module: {
      id: '00112233445566778899AABBCCDDEEFF0',
      name: 'libchrome_dll.dylib',
      sha256: 'a'.repeat(64),
    },
    schemaVersion: 1,
    scope: 'latest_incident',
  }
  const manifestContents = Buffer.from(`${JSON.stringify(manifest)}\n`)
  await fs.writeFile(path.join(root, 'manifest.json'), manifestContents)
  const checksumPayloads = new Map(payloads)
  checksumPayloads.set('manifest.json', manifestContents)
  await fs.writeFile(
    path.join(root, 'checksums.sha256'),
    [...checksumPayloads]
      .map(([relative, contents]) => `${digest(contents)}  ${relative}`)
      .join('\n') + '\n',
  )
  const archive = `${root}.zip`
  await run('zip', ['-qry', archive, '.'], { check: true, cwd: root })
  return archive
}

test('inspectDiagnosticsBundle validates manifest and checksums', async () => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'fp-diagnostics-'))
  try {
    const archive = await createBundle(root)
    const result = await inspectDiagnosticsBundle(archive, {
      expectedScope: 'latest_incident',
      forbiddenValues: ['secret-canary'],
    })
    assert.equal(result.crashCount, 0)
    assert.equal(result.manifest.module.name, 'libchrome_dll.dylib')
  } finally {
    await fs.rm(root, { recursive: true, force: true })
    await fs.rm(`${root}.zip`, { force: true })
  }
})

test('inspectDiagnosticsBundle rejects a secret canary', async () => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'fp-diagnostics-'))
  try {
    const archive = await createBundle(root, 'secret-canary')
    await assert.rejects(
      inspectDiagnosticsBundle(archive, {
        forbiddenValues: ['secret-canary'],
      }),
      /Forbidden text/,
    )
  } finally {
    await fs.rm(root, { recursive: true, force: true })
    await fs.rm(`${root}.zip`, { force: true })
  }
})
