// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

import assert from 'node:assert/strict'
import test from 'node:test'

import {
  DEFAULT_SOURCE_SPECS,
  auditSource,
  auditSources,
  disallowedEnglish,
} from '../audit_zh_visible_text.mjs'

test('default coverage includes corrected fingerprint sources and messages', () => {
  const specs = new Map(DEFAULT_SOURCE_SPECS.map((spec) => [spec.file, spec]))
  assert.equal(
    specs.get('components/fingerprint_browser/resources/fingerprint_guide.html')
      ?.optional,
    true,
  )
  assert.ok(
    specs.has('components/fingerprint_browser/browser/profile_proxy_config.cc'),
  )
  assert.ok(
    specs.has(
      'browser/fingerprint_browser/diagnostics/diagnostics_exporter.cc',
    ),
  )

  const generatedResources = specs.get('app/brave_generated_resources.grd')
  const findings = auditSource({
    ...generatedResources,
    source: `
      <message name="IDS_OTHER">Unrelated English</message>
      <message name="IDS_SHOW_FINGERPRINT_GUIDE">Show guide</message>
      <message name="IDS_FINGERPRINT_GUIDE_TITLE">Guide title</message>
    `,
  })
  assert.deepEqual(
    findings.map(({ text }) => text),
    ['Show guide', 'Guide title'],
  )
})

test('skips optional sources missing with ENOENT', async () => {
  const findings = await auditSources([
    {
      file: `missing-optional-${process.pid}-${Date.now()}.html`,
      optional: true,
    },
  ])
  assert.deepEqual(findings, [])
})

test('rejects required sources missing with ENOENT', async () => {
  await assert.rejects(
    auditSources([
      { file: `missing-required-${process.pid}-${Date.now()}.html` },
    ]),
    { code: 'ENOENT' },
  )
})

test('allows technical tokens and machine data', () => {
  assert.equal(
    disallowedEnglish(
      'HTTP HTTPS SOCKS5 WebRTC UA UA-CH WebGL WebGPU IP IANA JSON ZIP '
        + 'Profile Crashpad Chrome Web Store',
    ),
    '',
  )
  assert.equal(
    disallowedEnglish(
      'https://proxy.example.test /tmp/report.json profile_id '
        + 'America/New_York 203.0.113.10 US 42',
    ),
    '',
  )
})

test('allows Cookie and Navigator standard names but rejects adjacent English', () => {
  assert.equal(disallowedEnglish('Cookie 已启用，Navigator 信息已更新'), '')
  assert.equal(
    disallowedEnglish('Cookie settings 已保存，Navigator data 已更新'),
    'settings data',
  )
})

test('reports ordinary English in HTML visible text and attributes', () => {
  const findings = auditSource({
    file: 'sample.html',
    source: `
      <style>.label { color: red; }</style>
      <p>HTTP 代理已启用</p>
      <button title="Save settings">保存</button>
      <p>Connection failed</p>
      <code>https://proxy.example.test</code>
    `,
  })
  assert.deepEqual(
    findings.map(({ english }) => english),
    ['Connection failed', 'Save settings'],
  )
})

test('reports ordinary English assigned to code visibility sinks', () => {
  const findings = auditSource({
    file: 'sample.ts',
    source: `
      status.textContent = 'HTTPS 已连接'
      status.textContent = state === 'active' ? 'HTTP 已连接' : 'HTTPS 已连接'
      status.textContent = ready ? 'Connection ready' : 'Connection failed'
      addSummary('WebGL', value)
      addObservedRow('Screen color depth', depth)
      const internalMessage = 'Not visible'
    `,
  })
  assert.deepEqual(
    findings.map(({ text }) => text),
    ['Connection ready', 'Connection failed', 'Screen color depth'],
  )
})

test('filters shared resource messages and rejects ordinary English', () => {
  const findings = auditSource({
    file: 'strings.grdp',
    messageNamePattern: /FINGERPRINT/,
    source: `
      <message name="IDS_OTHER">Unrelated English</message>
      <message name="IDS_FINGERPRINT_PROTOCOL">HTTP、HTTPS</message>
      <message name="IDS_FINGERPRINT_SAVE">Save proxy settings</message>
    `,
  })
  assert.deepEqual(
    findings.map(({ text }) => text),
    ['Save proxy settings'],
  )
})
