import fs from 'node:fs/promises'
import path from 'node:path'

import {pathExists, sha256} from './system.mjs'

const REQUIRED_NATIVE_SCREENSHOTS = [
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

const REQUIRED_NATIVE_INTERACTIONS = [
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
]

export async function importNativeEvidence(destination, artifacts) {
  const source = process.env.FP_QA_NATIVE_UI_EVIDENCE_DIR
  if (!source) {
    return {
      reason: 'FP_QA_NATIVE_UI_EVIDENCE_DIR is required for native toolbar/menu evidence',
      status: 'BLOCKED',
    }
  }
  const manifestFile = path.join(source, 'manifest.json')
  if (!(await pathExists(manifestFile))) {
    return {
      reason: 'Native UI evidence manifest.json is required',
      status: 'BLOCKED',
    }
  }
  const manifest = JSON.parse(await fs.readFile(manifestFile, 'utf8'))
  if (manifest.libchromeSha256 !== artifacts.libchrome.source.sha256 ||
      manifest.resourcesSha256 !== artifacts.resources.source.sha256 ||
      manifest.chromiumResourcesSha256 !== artifacts.resources.chromiumSource.sha256) {
    return {
      reason: 'Native UI evidence artifact hashes do not match this QA build',
      status: 'BLOCKED',
    }
  }
  const capturedAt = Date.parse(manifest.capturedAt)
  const artifactMtime = Math.max(
    artifacts.libchrome.source.mtimeMs,
    artifacts.resources.source.mtimeMs,
    artifacts.resources.chromiumSource.mtimeMs)
  if (!Number.isFinite(capturedAt) || capturedAt < artifactMtime) {
    return {
      reason: 'Native UI evidence was captured before this QA build',
      status: 'BLOCKED',
    }
  }
  const missing = []
  const records = []
  await fs.mkdir(destination, {recursive: true})
  for (const name of REQUIRED_NATIVE_SCREENSHOTS) {
    const file = path.join(source, name)
    if (!(await pathExists(file))) {
      missing.push(name)
      continue
    }
    const target = path.join(destination, name)
    const sourceHash = await sha256(file)
    if (manifest.files?.[name] !== sourceHash) {
      missing.push(`${name} (hash mismatch)`)
      continue
    }
    await fs.copyFile(file, target)
    records.push({file: target, sha256: sourceHash})
  }
  if (missing.length > 0) {
    return {
      missing,
      reason: `Native UI evidence is missing ${missing.length} required screenshots`,
      status: 'BLOCKED',
    }
  }
  const interactions = REQUIRED_NATIVE_INTERACTIONS.map(id => ({
    id,
    ...(manifest.interactions?.[id] || {}),
  }))
  const incompleteInteractions = interactions.filter(interaction =>
    interaction.status !== 'PASS' || !String(interaction.reason || '').trim())
  if (incompleteInteractions.length > 0) {
    return {
      incompleteInteractions: incompleteInteractions.map(interaction => interaction.id),
      reason: `${incompleteInteractions.length} native UI interactions lack PASS evidence`,
      status: 'BLOCKED',
    }
  }
  return {interactions, manifest: manifestFile, records, status: 'PASS'}
}
