// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.
import assert from 'node:assert/strict'
import test from 'node:test'

import { assertProbeConsistency } from '../lib/profile.mjs'

function probe() {
  const basic = {
    hardwareConcurrency: 8,
    language: 'en-US',
    languages: ['en-US', 'en'],
    platform: 'MacIntel',
    userAgent: 'Persona UA',
  }
  return {
    basic,
    dedicatedWorker: { ...basic },
    iframe: { ...basic },
    serviceWorker: { ...basic },
    sharedWorker: { ...basic },
    uaData: {
      brands: [{ brand: 'Persona', version: '1' }],
      platform: 'macOS',
    },
    wireHeaders: {
      'accept-language': 'en-US,en;q=0.9',
      'sec-ch-ua': '"Persona";v="1"',
      'sec-ch-ua-platform': '"macOS"',
      'user-agent': 'Persona UA',
    },
  }
}

test('assertProbeConsistency accepts matching document, workers, and wire headers', () => {
  assert.doesNotThrow(() => assertProbeConsistency(probe()))
})

test('assertProbeConsistency rejects worker and wire mismatches', () => {
  const workerMismatch = probe()
  workerMismatch.serviceWorker.platform = 'Win32'
  assert.throws(
    () => assertProbeConsistency(workerMismatch),
    /serviceWorker platform/,
  )

  const wireMismatch = probe()
  wireMismatch.wireHeaders['sec-ch-ua-platform'] = '"Windows"'
  assert.throws(() => assertProbeConsistency(wireMismatch), /Wire UA-CH/)

  const brandMismatch = probe()
  brandMismatch.wireHeaders['sec-ch-ua'] = '"Different";v="1"'
  assert.throws(() => assertProbeConsistency(brandMismatch), /Wire UA-CH brand/)
})
