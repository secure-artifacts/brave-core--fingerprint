// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.
import assert from 'node:assert/strict'
import { createHash } from 'node:crypto'
import fs from 'node:fs/promises'
import path from 'node:path'
import test from 'node:test'
import { fileURLToPath } from 'node:url'

import {
  BUILD_PRODUCT_FULL_NAME,
  PRODUCT_FULL_NAME,
} from '../lib/product.mjs'
import { run } from '../lib/system.mjs'

const BRAVE_ROOT = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)),
  '../../../..',
)

async function sha256(file) {
  return createHash('sha256').update(await fs.readFile(file)).digest('hex')
}

test('all desktop branding channels use an ASCII-safe build identity', async () => {
  for (const suffix of ['', '.beta', '.dev', '.development', '.nightly']) {
    const branding = await fs.readFile(
      path.join(BRAVE_ROOT, 'app', 'theme', 'brave', 'BRANDING' + suffix),
      'utf8',
    )
    assert.match(
      branding,
      new RegExp('PRODUCT_FULLNAME=' + BUILD_PRODUCT_FULL_NAME),
    )
    assert.match(
      branding,
      new RegExp('PRODUCT_SHORTNAME=' + BUILD_PRODUCT_FULL_NAME),
    )
    assert.doesNotMatch(branding, /^PRODUCT_(?:FULLNAME|SHORTNAME)=.*Brave/m)
  }
})

test('macOS app overrides build identity with the Chinese display name', async () => {
  const [build, script, strings] = await Promise.all([
    fs.readFile(path.join(BRAVE_ROOT, 'BUILD.gn'), 'utf8'),
    fs.readFile(
      path.join(BRAVE_ROOT, 'build', 'mac', 'tweak_info_plist.py'),
      'utf8',
    ),
    fs.readFile(path.join(BRAVE_ROOT, 'app', 'brave_strings.grd'), 'utf8'),
  ])
  assert.match(
    build,
    /--display_name_utf8_hex=e68c87e7bab9e6b58fe8a788e599a8/,
  )
  assert.match(script, /plist\['CFBundleDisplayName'\] = display_name/)
  assert.match(script, /plist\['CFBundleName'\] = display_name/)
  assert.match(strings, new RegExp('>\\s*' + PRODUCT_FULL_NAME + '\\s*<'))
})

test('custom product logo sources have expected dimensions and alpha', async () => {
  const files = [
    ['app/theme/brave/fingerprint_browser/logo_master_1024.png', 1024, 1024],
    ['app/theme/brave/product_logo_24.png', 24, 24],
    ['app/theme/brave/product_logo_256.png', 256, 256],
    ['app/theme/default_100_percent/brave/product_logo_16.png', 16, 16],
    ['app/theme/default_200_percent/brave/product_logo_32.png', 64, 64],
  ]
  for (const [relative, width, height] of files) {
    const result = await run(
      '/usr/bin/sips',
      [
        '-g',
        'pixelWidth',
        '-g',
        'pixelHeight',
        '-g',
        'hasAlpha',
        path.join(BRAVE_ROOT, relative),
      ],
      { check: true },
    )
    assert.match(result.stdout, new RegExp('pixelWidth: ' + width))
    assert.match(result.stdout, new RegExp('pixelHeight: ' + height))
    assert.match(result.stdout, /hasAlpha: yes/)
  }
})

test('macOS channel icons use one custom asset set', async () => {
  const directories = ['', 'beta', 'dev', 'development', 'nightly']
  const icnsHashes = []
  const catalogHashes = []
  for (const directory of directories) {
    const base = path.join(BRAVE_ROOT, 'app', 'theme', 'brave', 'mac', directory)
    icnsHashes.push(await sha256(path.join(base, 'app.icns')))
    catalogHashes.push(await sha256(path.join(base, 'Assets.car')))
  }
  assert.equal(new Set(icnsHashes).size, 1)
  assert.equal(new Set(catalogHashes).size, 1)

  if (process.platform === 'darwin') {
    const result = await run(
      '/usr/bin/xcrun',
      [
        'assetutil',
        '--info',
        path.join(
          BRAVE_ROOT,
          'app',
          'theme',
          'brave',
          'mac',
          'development',
          'Assets.car',
        ),
      ],
      { check: true },
    )
    const renditions = JSON.parse(result.stdout)
    assert.equal(
      renditions.filter((item) => item.Name === 'AppIcon').length >= 10,
      true,
    )
    assert.equal(
      renditions.some((item) => String(item.Name || '').includes('Brave')),
      false,
    )
  }
})

test('product vectors no longer contain the Brave product mark', async () => {
  for (const relative of [
    'app/theme/brave/product_logo.svg',
    'app/theme/brave/product_logo_animation.svg',
    'components/omnibox/browser/vector_icons/brave/product.icon',
    'components/omnibox/browser/vector_icons/brave/product_chrome_refresh.icon',
  ]) {
    const source = await fs.readFile(path.join(BRAVE_ROOT, relative), 'utf8')
    assert.doesNotMatch(source, /BRAVE ORANGE|FF5500|FF2000/)
  }
})
