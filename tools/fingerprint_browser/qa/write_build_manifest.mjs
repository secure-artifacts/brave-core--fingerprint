#!/usr/bin/env node

import path from 'node:path'
import {fileURLToPath} from 'node:url'

import {writeBuildManifest} from './lib/artifacts.mjs'

const mode = process.argv[2]
const braveRoot = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)), '../../..')
const chromiumRoot = path.resolve(braveRoot, '..')
const outDir = path.join(chromiumRoot, process.argv[3] || 'out/Component_arm64')

const result = await writeBuildManifest({braveRoot, mode, outDir})
console.log(`Build source manifest: ${result.file}`)
