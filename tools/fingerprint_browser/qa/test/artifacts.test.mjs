// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.
import assert from 'node:assert/strict'
import fs from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'
import test from 'node:test'

import { SOURCE_GROUPS, unsignedMachOSha256 } from '../lib/artifacts.mjs'

test('artifact source groups cover fingerprint browser compiled inputs', () => {
  for (const source of [
    '../third_party/blink/renderer/modules/plugins/dom_plugin_array.cc',
    '../third_party/blink/renderer/platform/fonts/font_fallback_list.cc',
    'app/theme/brave/BRANDING.development',
    'browser/brave_shields/brave_shields_web_contents_observer.cc',
    'chromium_src/third_party/blink/renderer/platform/fonts/font_fallback_list.cc',
    'components/brave_shields/core/common/shields_settings.mojom',
    'components/omnibox/browser/vector_icons/brave/product.icon',
    'third_party/blink/renderer/BUILD.gn',
    'third_party/blink/renderer/core/farbling/brave_session_cache.h',
  ]) {
    assert.ok(SOURCE_GROUPS.native.includes(source), source)
  }
  for (const source of [
    'browser/resources/settings/br/privacy_page.ts',
    'browser/resources/settings/br/settings_menu.ts',
    'browser/resources/settings/brave_routes.ts',
  ]) {
    assert.ok(SOURCE_GROUPS.braveResources.includes(source), source)
  }
  assert.ok(
    SOURCE_GROUPS.chromiumResources.includes(
      'app/theme/brave/product_logo.svg',
    ),
  )
  assert.ok(SOURCE_GROUPS.localeResources.includes('app/brave_strings.grd'))
  assert.ok(
    !SOURCE_GROUPS.scaledResources.includes('app/brave_settings_strings.grdp'),
  )
  assert.ok(
    SOURCE_GROUPS.scaledResources.includes(
      'app/theme/default_100_percent/brave',
    ),
  )
})

function fakeSignedMachO(signature) {
  const header = Buffer.alloc(32)
  header.writeUInt32LE(0xfeedfacf, 0)
  header.writeUInt32LE(2, 16)
  header.writeUInt32LE(88, 20)

  const linkedit = Buffer.alloc(72)
  linkedit.writeUInt32LE(0x19, 0)
  linkedit.writeUInt32LE(linkedit.length, 4)
  linkedit.write('__LINKEDIT', 8, 'ascii')
  linkedit.writeBigUInt64LE(BigInt(4096 + signature.length), 32)
  linkedit.writeBigUInt64LE(BigInt(7 + signature.length), 48)

  const command = Buffer.alloc(16)
  command.writeUInt32LE(0x1d, 0)
  command.writeUInt32LE(command.length, 4)
  command.writeUInt32LE(header.length + linkedit.length + command.length + 7, 8)
  command.writeUInt32LE(signature.length, 12)

  return Buffer.concat([
    header,
    linkedit,
    command,
    Buffer.from('payload'),
    signature,
  ])
}

test('unsignedMachOSha256 ignores ad-hoc signature payload and size', async () => {
  const directory = await fs.mkdtemp(
    path.join(os.tmpdir(), 'fp-qa-artifact-test-'),
  )
  try {
    const first = path.join(directory, 'first.dylib')
    const second = path.join(directory, 'second.dylib')
    await fs.writeFile(first, fakeSignedMachO(Buffer.from('signature-one')))
    await fs.writeFile(
      second,
      fakeSignedMachO(Buffer.from('different-signature-two')),
    )

    assert.equal(
      await unsignedMachOSha256(first),
      await unsignedMachOSha256(second),
    )
  } finally {
    await fs.rm(directory, { recursive: true, force: true })
  }
})
