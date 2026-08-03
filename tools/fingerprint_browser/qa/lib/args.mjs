// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const MODES = new Set(['smoke', 'proxy', 'full', 'soak'])
const DEFAULT_BRAVE_ROOT = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)),
  '../../../..',
)

export function usage() {
  return `Usage: run_qa.mjs --mode smoke|proxy|full|soak [options]

Options:
  --app <path>              QA .app path
  --results-dir <path>      Parent results directory
  --proxy-fixtures <path>   0600 JSON containing an HTTP proxy fixture
  --baseline-dir <path>     Approved screenshot baselines
  --duration-minutes <n>    Soak duration, default 60
  --keep-profile            Keep this run's temporary profiles
  --skip-code-tests         Skip C++ tests (never accepted for Full delivery)
  --no-prepare-app          Verify app without copying current artifacts
  --help                    Show this message
`
}

export function parseArgs(
  argv,
  { braveRoot = DEFAULT_BRAVE_ROOT, cwd = process.cwd() } = {},
) {
  const values = {}
  const flags = new Set()
  const valueOptions = new Set([
    '--mode',
    '--app',
    '--results-dir',
    '--proxy-fixtures',
    '--baseline-dir',
    '--duration-minutes',
  ])
  const flagOptions = new Set([
    '--help',
    '--keep-profile',
    '--skip-code-tests',
    '--no-prepare-app',
  ])

  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index]
    if (flagOptions.has(argument)) {
      flags.add(argument)
      continue
    }
    if (!valueOptions.has(argument)) {
      throw new Error(`Unknown option: ${argument}`)
    }
    const value = argv[index + 1]
    if (!value || value.startsWith('--')) {
      throw new Error(`Missing value for ${argument}`)
    }
    values[argument] = value
    index += 1
  }

  if (flags.has('--help')) {
    return { help: true }
  }

  const mode = values['--mode'] || 'smoke'
  if (!MODES.has(mode)) {
    throw new Error(`Invalid mode: ${mode}`)
  }

  braveRoot = path.resolve(braveRoot)
  const chromiumRoot = path.resolve(braveRoot, '..')
  const outDir = path.join(chromiumRoot, 'out', 'Component_arm64')
  const app = values['--app']
    ? path.resolve(cwd, values['--app'])
    : path.join(
        outDir,
        'fingerprint-browser-qa',
        'Brave Browser Development QA.app',
      )
  const resultsDir = values['--results-dir']
    ? path.resolve(cwd, values['--results-dir'])
    : path.join(outDir, 'qa-results')
  const durationMinutes = Number(values['--duration-minutes'] || 60)
  if (!Number.isFinite(durationMinutes) || durationMinutes <= 0) {
    throw new Error('--duration-minutes must be greater than zero')
  }

  return {
    app,
    baselineDir: values['--baseline-dir']
      ? path.resolve(cwd, values['--baseline-dir'])
      : path.join(braveRoot, 'tools', 'fingerprint_browser', 'qa', 'baselines'),
    braveRoot,
    chromiumRoot,
    durationMinutes,
    help: false,
    keepProfile: flags.has('--keep-profile'),
    mode,
    outDir,
    prepareApp: !flags.has('--no-prepare-app'),
    proxyFixtures: values['--proxy-fixtures']
      ? path.resolve(cwd, values['--proxy-fixtures'])
      : null,
    resultsDir,
    skipCodeTests: flags.has('--skip-code-tests'),
  }
}
