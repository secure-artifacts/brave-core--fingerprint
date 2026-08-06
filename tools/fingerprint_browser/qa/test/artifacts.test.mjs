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
  artifactsForBuildMode,
  nativeArtifactNamesForSource,
  SOURCE_GROUPS,
  unsignedMachOSha256,
} from '../lib/artifacts.mjs'

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
    'third_party/blink/renderer/platform/brave_audio_farbling_helper.cc',
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

test('native freshness follows component-build ownership', () => {
  assert.deepEqual(
    nativeArtifactNamesForSource(
      'third_party/blink/renderer/core/farbling/brave_session_cache.cc',
    ),
    ['libblink_core.dylib'],
  )
  assert.deepEqual(
    nativeArtifactNamesForSource(
      'chromium_src/third_party/blink/renderer/modules/webaudio/audio_buffer.cc',
    ),
    ['libblink_modules.dylib'],
  )
  assert.deepEqual(
    nativeArtifactNamesForSource(
      'patches/third_party-blink-renderer-modules-webaudio-audio_buffer.cc.patch',
    ),
    ['libblink_modules.dylib'],
  )
  assert.deepEqual(
    nativeArtifactNamesForSource(
      'third_party/blink/renderer/platform/brave_audio_farbling_helper.cc',
    ),
    ['libblink_platform.dylib'],
  )
  assert.deepEqual(
    nativeArtifactNamesForSource('../net/http/http_network_transaction.cc'),
    ['libnet.dylib'],
  )
  assert.deepEqual(
    nativeArtifactNamesForSource(
      'chromium_src/content/browser/service_worker/service_worker_host.cc',
    ),
    ['libcontent.dylib'],
  )
  assert.deepEqual(
    nativeArtifactNamesForSource(
      'chromium_src/third_party/blink/public/mojom/worker/worker_content_settings_proxy.mojom',
    ),
    [
      'libblink_common.dylib',
      'libblink_platform.dylib',
      'libblink_platform_media.dylib',
      'libmojom_platform_shared.dylib',
    ],
  )
  assert.deepEqual(
    nativeArtifactNamesForSource(
      '../chrome/browser/net/profile_network_context_service.cc',
    ),
    ['libchrome_dll.dylib'],
  )
  assert.deepEqual(
    nativeArtifactNamesForSource(
      'components/brave_shields/core/common/shields_settings.mojom',
    ),
    [
      'libbrave_shields_mojom.dylib',
      'libbrave_shields_mojom_shared.dylib',
      'libblink_platform.dylib',
    ],
  )
  assert.deepEqual(
    nativeArtifactNamesForSource('browser/fingerprint_browser/service.cc'),
    ['libchrome_dll.dylib'],
  )
})

test('resources-only manifests retain native build identities', () => {
  const previous = {
    native: 'old-libchrome',
    nativeSet: { count: 2, digest: 'old-native-set' },
    network: 'old-network',
  }
  const current = {
    braveResources: 'new-resources',
    native: 'new-libchrome',
    nativeSet: { count: 3, digest: 'new-native-set' },
    network: 'new-network',
  }

  assert.deepEqual(artifactsForBuildMode('resources', current, previous), {
    braveResources: 'new-resources',
    native: 'old-libchrome',
    nativeSet: { count: 2, digest: 'old-native-set' },
    network: 'old-network',
  })
  assert.equal(artifactsForBuildMode('cpp', current, previous), current)
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
