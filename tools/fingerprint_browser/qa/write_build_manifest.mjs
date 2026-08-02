// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

import path from 'node:path'
import { fileURLToPath } from 'node:url'

import { writeBuildManifest } from './lib/artifacts.mjs'

const mode = process.argv[2]
const braveRoot = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)),
  '../../..',
)
const chromiumRoot = path.resolve(braveRoot, '..')
const outDir = path.join(chromiumRoot, process.argv[3] || 'out/Component_arm64')

const result = await writeBuildManifest({ braveRoot, mode, outDir })
console.log(`Build source manifest: ${result.file}`)
