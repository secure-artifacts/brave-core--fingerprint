// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

import { createHash } from 'node:crypto'
import fs from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'

import { run, sha256 } from './system.mjs'

const LIMITS = {
  latest_incident: { bytes: 100 * 1024 * 1024, reports: 10 },
  last_7_days: { bytes: 250 * 1024 * 1024, reports: 20 },
}

const REQUIRED_FILES = [
  'README.txt',
  'manifest.json',
  'checksums.sha256',
  'state/browser.json',
  'state/extensions.json',
  'state/fingerprint.json',
  'state/profiles.json',
  'state/proxy.json',
]

const TEXT_EXTENSIONS = new Set(['.json', '.jsonl', '.log', '.txt'])

async function listFiles(root, current = root) {
  const files = []
  for (const entry of await fs.readdir(current, { withFileTypes: true })) {
    const target = path.join(current, entry.name)
    if (entry.isDirectory()) {
      files.push(...await listFiles(root, target))
    } else if (entry.isFile()) {
      files.push(path.relative(root, target).split(path.sep).join('/'))
    } else {
      throw new Error(`Unsupported diagnostic archive entry: ${target}`)
    }
  }
  return files.sort()
}

function assertSafeEntries(entries) {
  const unique = new Set()
  for (const entry of entries) {
    const normalized = path.posix.normalize(entry)
    if (
      !entry
      || entry.startsWith('/')
      || normalized === '..'
      || normalized.startsWith('../')
      || normalized !== entry.replace(/\/$/, '')
    ) {
      throw new Error(`Unsafe diagnostic archive path: ${entry}`)
    }
    if (unique.has(normalized)) {
      throw new Error(`Duplicate diagnostic archive path: ${normalized}`)
    }
    unique.add(normalized)
  }
}

function parseChecksums(contents) {
  const checksums = new Map()
  for (const line of contents.trim().split('\n')) {
    const match = line.match(/^([0-9a-f]{64})  (.+)$/)
    if (!match) throw new Error(`Invalid checksum line: ${line}`)
    if (checksums.has(match[2])) {
      throw new Error(`Duplicate checksum path: ${match[2]}`)
    }
    checksums.set(match[2], match[1])
  }
  return checksums
}

async function hashBuffer(contents) {
  return createHash('sha256').update(contents).digest('hex')
}

export async function inspectDiagnosticsBundle(
  archive,
  { forbiddenValues = [], expectedScope = null } = {},
) {
  const archiveStat = await fs.stat(archive)
  const listResult = await run('unzip', ['-Z1', archive], { check: true })
  const archiveEntries = listResult.stdout
    .split('\n')
    .filter((entry) => entry && !entry.endsWith('/'))
  assertSafeEntries(archiveEntries)

  const extractionRoot = await fs.mkdtemp(
    path.join(os.tmpdir(), 'fingerprint-diagnostics-inspect-'),
  )
  try {
    await run('unzip', ['-qq', archive, '-d', extractionRoot], { check: true })
    const files = await listFiles(extractionRoot)
    for (const required of REQUIRED_FILES) {
      if (!files.includes(required)) {
        throw new Error(`Diagnostic bundle is missing ${required}`)
      }
    }

    const manifest = JSON.parse(
      await fs.readFile(path.join(extractionRoot, 'manifest.json'), 'utf8'),
    )
    const limits = LIMITS[manifest.scope]
    if (!limits) throw new Error(`Unsupported manifest scope: ${manifest.scope}`)
    if (expectedScope && manifest.scope !== expectedScope) {
      throw new Error(
        `Expected scope ${expectedScope}, found ${manifest.scope}`,
      )
    }
    if (archiveStat.size > limits.bytes) {
      throw new Error(`Diagnostic archive exceeds ${limits.bytes} bytes`)
    }

    const dumpFiles = files.filter(
      (file) => file.startsWith('crashes/') && file.endsWith('.dmp'),
    )
    if (dumpFiles.length > limits.reports) {
      throw new Error(`Diagnostic bundle contains ${dumpFiles.length} dumps`)
    }
    if (manifest.crashCount !== dumpFiles.length) {
      throw new Error('Manifest crash count does not match archive contents')
    }
    if (
      !manifest.module?.name
      || !manifest.module?.id
      || !manifest.module?.sha256
    ) {
      throw new Error('Manifest does not contain exact module identity')
    }

    const checksums = parseChecksums(
      await fs.readFile(path.join(extractionRoot, 'checksums.sha256'), 'utf8'),
    )
    const expectedChecksumPaths = files.filter(
      (file) => file !== 'checksums.sha256',
    )
    if (checksums.has('checksums.sha256')) {
      throw new Error('Checksum file must not hash itself')
    }
    if (checksums.size !== expectedChecksumPaths.length) {
      throw new Error('Checksum inventory does not match archive contents')
    }
    for (const relative of expectedChecksumPaths) {
      const actual = await sha256(path.join(extractionRoot, relative))
      if (checksums.get(relative) !== actual) {
        throw new Error(`Checksum mismatch: ${relative}`)
      }
    }

    const manifestFiles = new Map(
      (manifest.files || []).map((entry) => [entry.path, entry]),
    )
    const payloadFiles = expectedChecksumPaths.filter(
      (file) => file !== 'manifest.json',
    )
    if (manifestFiles.size !== payloadFiles.length) {
      throw new Error('Manifest payload inventory is incomplete')
    }
    for (const relative of payloadFiles) {
      const entry = manifestFiles.get(relative)
      const contents = await fs.readFile(path.join(extractionRoot, relative))
      if (
        !entry
        || Number(entry.sizeBytes) !== contents.length
        || entry.sha256 !== await hashBuffer(contents)
      ) {
        throw new Error(`Manifest payload mismatch: ${relative}`)
      }
    }

    for (const relative of files) {
      if (!TEXT_EXTENSIONS.has(path.extname(relative))) continue
      const contents = await fs.readFile(path.join(extractionRoot, relative), 'utf8')
      for (const forbidden of forbiddenValues) {
        if (forbidden && contents.includes(forbidden)) {
          throw new Error(`Forbidden text found in ${relative}`)
        }
      }
    }

    return {
      archive,
      archiveSha256: await sha256(archive),
      bytes: archiveStat.size,
      crashCount: dumpFiles.length,
      files,
      manifest,
    }
  } finally {
    await fs.rm(extractionRoot, { recursive: true, force: true })
  }
}
