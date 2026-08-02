import assert from 'node:assert/strict'
import test from 'node:test'

import {analyzeThirdPartyScan} from '../scenarios/full.mjs'

const passing = {
  bodyText: 'Public IP 203.0.113.10',
  expectedIp: '203.0.113.10',
  expectedLanguage: 'en-US',
  expectedTimezone: 'America/New_York',
  languages: ['en-US', 'en'],
  name: 'browserleaks',
  responseStatus: 200,
  timezone: 'America/New_York',
  webRtcCandidates: [
    'candidate:1 1 udp 1 203.0.113.10 1234 typ srflx raddr 0.0.0.0 rport 0',
  ],
}

test('third-party scan accepts matching proxy surfaces', () => {
  const result = analyzeThirdPartyScan(passing)
  assert.equal(result.pass, true)
  assert.deepEqual(result.failures, [])
})

test('third-party scan rejects lie and mismatch counters', () => {
  const result = analyzeThirdPartyScan({
    ...passing,
    bodyText: 'Public IP 203.0.113.10\nLies (2)\nMismatched: yes',
    name: 'creepjs',
  })
  assert.equal(result.pass, false)
  assert.equal(result.lieSignals.length, 2)
})

test('third-party scan rejects language timezone IP and WebRTC leaks', () => {
  const result = analyzeThirdPartyScan({
    ...passing,
    bodyText: 'Public IP 198.51.100.5',
    languages: ['fr-FR'],
    timezone: 'Europe/Paris',
    webRtcCandidates: [
      'candidate:1 1 udp 1 198.51.100.5 1234 typ srflx raddr 0.0.0.0 rport 0',
    ],
  })
  assert.equal(result.pass, false)
  assert.equal(result.failures.length, 4)
})
