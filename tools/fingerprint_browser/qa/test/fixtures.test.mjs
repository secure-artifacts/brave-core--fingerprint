// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.
import assert from 'node:assert/strict'
import fs from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'
import test from 'node:test'

import { loadProxyFixtures } from '../lib/fixtures.mjs'

function fixture() {
  const base = {
    countryCode: 'US',
    expectedIp: '203.0.113.10',
    geoVerifyUrl: 'https://geo.example.test/json',
    host: 'proxy.example',
    language: 'en-US',
    latitude: 40.7,
    longitude: -74,
    password: 'secret',
    port: 1080,
    timezone: 'America/New_York',
    username: 'qa',
  }
  return { http: { ...base }, socks5: { ...base } }
}

test('loadProxyFixtures requires explicit input', async () => {
  assert.equal((await loadProxyFixtures(null)).status, 'BLOCKED')
})

test('loadProxyFixtures enforces 0600', async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'fp-qa-fixture-'))
  const file = path.join(directory, 'proxies.json')
  try {
    await fs.writeFile(file, JSON.stringify(fixture()), { mode: 0o644 })
    await assert.rejects(() => loadProxyFixtures(file), /must be 0600/)
    await fs.chmod(file, 0o600)
    const loaded = await loadProxyFixtures(file)
    assert.equal(loaded.status, 'PASS')
    assert.equal(loaded.http.scheme, 'http')
    assert.equal(loaded.socks5.scheme, 'socks5')
  } finally {
    await fs.rm(directory, { recursive: true, force: true })
  }
})

test('loadProxyFixtures exposes available fixtures while blocking missing protocols', async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'fp-qa-fixture-'))
  const file = path.join(directory, 'proxies.json')
  try {
    await fs.writeFile(file, JSON.stringify({ http: fixture().http }), {
      mode: 0o600,
    })
    const loaded = await loadProxyFixtures(file)
    assert.equal(loaded.status, 'BLOCKED')
    assert.deepEqual(loaded.missing, ['socks5'])
    assert.equal(loaded.http.scheme, 'http')
    assert.equal(loaded.socks5, null)
  } finally {
    await fs.rm(directory, { recursive: true, force: true })
  }
})
