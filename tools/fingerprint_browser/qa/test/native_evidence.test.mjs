import assert from 'node:assert/strict'
import fs from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'
import test from 'node:test'

import {importNativeEvidence} from '../lib/native_evidence.mjs'
import {sha256} from '../lib/system.mjs'

const NAMES = [
  'toolbar-normal.png',
  'toolbar-hover.png',
  'toolbar-pressed.png',
  'action-required.png',
  'sidebar.png',
  'more-tools.png',
  'profile-picker.png',
  'extension-install-confirmation.png',
  'extension-installed-toolbar.png',
  'extension-popup.png',
]

const INTERACTIONS = Object.fromEntries([
  'pre-extension-shields',
  'pre-extension-vpn',
  'pre-extension-wallet',
  'pre-extension-ai',
  'pre-extension-sidebar',
  'pre-extension-profile-menu',
  'pre-extension-more-tools',
  'pre-extension-action-required',
  'post-extension-shields',
  'post-extension-vpn',
  'post-extension-wallet',
  'post-extension-ai',
  'post-extension-sidebar',
  'post-extension-profile-menu',
  'post-extension-more-tools',
  'post-extension-action-required',
].map(id => [id, {reason: `${id} opened without a crash`, status: 'PASS'}]))

test('importNativeEvidence binds screenshots to current artifact hashes', async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'fp-qa-native-'))
  const source = path.join(directory, 'source')
  const destination = path.join(directory, 'destination')
  const previous = process.env.FP_QA_NATIVE_UI_EVIDENCE_DIR
  try {
    await fs.mkdir(source)
    const files = {}
    for (const name of NAMES) {
      const file = path.join(source, name)
      await fs.writeFile(file, name)
      files[name] = await sha256(file)
    }
    await fs.writeFile(path.join(source, 'manifest.json'), JSON.stringify({
      capturedAt: new Date(2000).toISOString(),
      chromiumResourcesSha256: 'chromium-resources',
      files,
      interactions: INTERACTIONS,
      libchromeSha256: 'lib',
      resourcesSha256: 'resources',
    }))
    process.env.FP_QA_NATIVE_UI_EVIDENCE_DIR = source
    const result = await importNativeEvidence(destination, {
      libchrome: {source: {mtimeMs: 1000, sha256: 'lib'}},
      resources: {
        chromiumSource: {mtimeMs: 1000, sha256: 'chromium-resources'},
        source: {mtimeMs: 1000, sha256: 'resources'},
      },
    })
    assert.equal(result.status, 'PASS')
    assert.equal(result.records.length, NAMES.length)
  } finally {
    if (previous === undefined) {
      delete process.env.FP_QA_NATIVE_UI_EVIDENCE_DIR
    } else {
      process.env.FP_QA_NATIVE_UI_EVIDENCE_DIR = previous
    }
    await fs.rm(directory, {recursive: true, force: true})
  }
})
