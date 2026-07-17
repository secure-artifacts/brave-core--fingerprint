import assert from 'node:assert/strict'
import fs from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'
import test from 'node:test'

import {qaBrowserArgs, seedProfilePreferences} from '../lib/browser.mjs'

test('macOS QA sessions isolate Keychain access', () => {
  const args = qaBrowserArgs({
    platform: 'darwin',
    port: 9222,
    profileDirectory: 'Default',
    profilePath: '/tmp/fingerprint-browser-test',
  })

  assert.ok(args.includes('--use-mock-keychain'))
  assert.equal(args.at(-1), 'about:blank')
})

test('QA session custom arguments precede initial URL', () => {
  const args = qaBrowserArgs({
    extraArgs: ['--force-dark-mode'],
    platform: 'linux',
    port: 9222,
    profileDirectory: 'Profile 1',
    profilePath: '/tmp/fingerprint-browser-test',
  })

  assert.equal(args.includes('--use-mock-keychain'), false)
  assert.ok(args.indexOf('--force-dark-mode') < args.indexOf('about:blank'))
})

test('production-semantic QA sessions omit test-type', () => {
  const args = qaBrowserArgs({
    platform: 'darwin',
    port: 9222,
    profileDirectory: 'Default',
    profilePath: '/tmp/fingerprint-browser-test',
    testType: false,
  })

  assert.equal(args.includes('--test-type'), false)
  assert.ok(args.includes('--use-mock-keychain'))
})

test('seedProfilePreferences merges nested profile values', async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'fp-qa-prefs-'))
  const preferencesFile = path.join(directory, 'Default', 'Preferences')
  try {
    await seedProfilePreferences(directory, 'Default', {
      browser: {theme: {color_scheme2: 1}},
    })
    await seedProfilePreferences(directory, 'Default', {
      brave: {dark_mode_migrated: true},
    })
    assert.deepEqual(JSON.parse(await fs.readFile(preferencesFile, 'utf8')), {
      brave: {dark_mode_migrated: true},
      browser: {theme: {color_scheme2: 1}},
    })
  } finally {
    await fs.rm(directory, {recursive: true, force: true})
  }
})
