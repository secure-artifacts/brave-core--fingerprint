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
  analyzeScreenshot,
  loadHumanVisualReview,
  writeVisualReviewBundle,
} from '../lib/visual.mjs'
import { run, sha256 } from '../lib/system.mjs'

test('analyzeScreenshot records SSIM and pixel comparison for approved baseline', async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'fp-qa-visual-'))
  const actual = path.join(directory, 'actual.png')
  const baselineDir = path.join(directory, 'baselines')
  const baseline = path.join(baselineDir, 'native', 'sample.png')
  try {
    await fs.mkdir(path.dirname(baseline), { recursive: true })
    await run('magick', ['-size', '32x32', 'pattern:checkerboard', actual], {
      check: true,
    })
    await fs.copyFile(actual, baseline)
    const result = await analyzeScreenshot({
      actual,
      baselineDir,
      baselineKey: path.join('native', 'sample.png'),
      diffDir: path.join(directory, 'diff'),
    })
    assert.equal(result.pass, true)
    assert.equal(result.baselineStatus, 'PASS')
    assert.equal(result.ssimDifference, 0)
    assert.equal(result.pixelDifferenceRatio, 0)
  } finally {
    await fs.rm(directory, { recursive: true, force: true })
  }
})

test('analyzeScreenshot rejects a connected pure-red UI component', async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'fp-qa-red-'))
  const actual = path.join(directory, 'actual.png')
  try {
    await run(
      'magick',
      [
        '-size',
        '64x64',
        'xc:white',
        '-fill',
        'red',
        '-draw',
        'rectangle 8,8 31,31',
        actual,
      ],
      { check: true },
    )
    const result = await analyzeScreenshot({
      actual,
      baselineDir: path.join(directory, 'baselines'),
      baselineKey: path.join('native', 'red.png'),
      checkRed: true,
      diffDir: path.join(directory, 'diff'),
    })
    assert.equal(result.pass, false)
    assert.ok(result.redComponents.maxArea > 24)
  } finally {
    await fs.rm(directory, { recursive: true, force: true })
  }
})

test('loadHumanVisualReview requires artifact-bound PASS/FAIL reasons', async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'fp-qa-review-'))
  const manifestFile = path.join(directory, 'review.json')
  const screenshot = path.join(directory, 'toolbar.png')
  const artifacts = {
    libchrome: { source: { sha256: 'lib' } },
    resources: {
      chromiumSource: { sha256: 'chromium-resources' },
      source: { sha256: 'resources' },
    },
  }
  const analyses = [{ actual: screenshot, baselineKey: 'native/toolbar.png' }]
  try {
    await fs.writeFile(screenshot, 'approved screenshot')
    await fs.writeFile(
      manifestFile,
      JSON.stringify({
        chromiumResourcesSha256: 'chromium-resources',
        libchromeSha256: 'lib',
        resourcesSha256: 'resources',
        reviews: {
          'native/toolbar.png': {
            reason: 'Icons are legible and correctly colored',
            screenshotSha256: await sha256(screenshot),
            status: 'PASS',
          },
        },
      }),
    )
    const result = await loadHumanVisualReview({
      analyses,
      artifacts,
      manifestFile,
    })
    assert.equal(result.status, 'PASS')
    assert.equal(
      result.reviews[0].reason,
      'Icons are legible and correctly colored',
    )
  } finally {
    await fs.rm(directory, { recursive: true, force: true })
  }
})

test('loadHumanVisualReview rejects approval for changed screenshot bytes', async () => {
  const directory = await fs.mkdtemp(
    path.join(os.tmpdir(), 'fp-qa-review-stale-'),
  )
  const manifestFile = path.join(directory, 'review.json')
  const screenshot = path.join(directory, 'toolbar.png')
  const artifacts = {
    libchrome: { source: { sha256: 'lib' } },
    resources: {
      chromiumSource: { sha256: 'chromium-resources' },
      source: { sha256: 'resources' },
    },
  }
  const analyses = [{ actual: screenshot, baselineKey: 'native/toolbar.png' }]
  try {
    await fs.writeFile(screenshot, 'approved screenshot')
    await fs.writeFile(
      manifestFile,
      JSON.stringify({
        chromiumResourcesSha256: 'chromium-resources',
        libchromeSha256: 'lib',
        resourcesSha256: 'resources',
        reviews: {
          'native/toolbar.png': {
            reason: 'Approved before recapture',
            screenshotSha256: await sha256(screenshot),
            status: 'PASS',
          },
        },
      }),
    )
    await fs.writeFile(screenshot, 'recaptured screenshot')
    const result = await loadHumanVisualReview({
      analyses,
      artifacts,
      manifestFile,
    })
    assert.equal(result.status, 'BLOCKED')
    assert.match(result.reason, /screenshot hashes do not match/)
  } finally {
    await fs.rm(directory, { recursive: true, force: true })
  }
})

test('writeVisualReviewBundle creates artifact-bound candidates and review files', async () => {
  const directory = await fs.mkdtemp(
    path.join(os.tmpdir(), 'fp-qa-review-bundle-'),
  )
  const screenshot = path.join(directory, 'toolbar.png')
  const artifacts = {
    libchrome: { source: { sha256: 'lib' } },
    resources: {
      chromiumSource: { sha256: 'chromium-resources' },
      source: { sha256: 'resources' },
    },
  }
  try {
    await fs.writeFile(screenshot, 'screenshot')
    const result = await writeVisualReviewBundle({
      analyses: [
        {
          actual: screenshot,
          baselineKey: 'native/toolbar.png',
          baselineStatus: 'MISSING',
          pass: true,
          reason: 'All automated visual checks passed',
        },
      ],
      artifacts,
      runDir: directory,
    })
    const manifest = JSON.parse(await fs.readFile(result.manifestFile, 'utf8'))
    assert.equal(manifest.libchromeSha256, 'lib')
    assert.deepEqual(manifest.reviews['native/toolbar.png'], {
      reason: '',
      screenshotSha256: await sha256(screenshot),
      status: '',
    })
    assert.equal(
      await fs.readFile(
        path.join(result.candidateDir, 'native', 'toolbar.png'),
        'utf8',
      ),
      'screenshot',
    )
    assert.match(
      await fs.readFile(result.galleryFile, 'utf8'),
      /native\/toolbar\.png/,
    )
    const reviewPage = await fs.readFile(result.htmlFile, 'utf8')
    assert.match(reviewPage, /指纹浏览器截图审批/)
    assert.match(reviewPage, /id="lightbox"/)
    assert.match(reviewPage, /导出审批结果/)
    assert.match(reviewPage, /candidate-baselines\/native\/toolbar\.png/)
    assert.match(reviewPage, /"libchromeSha256":"lib"/)
    const scripts = [
      ...reviewPage.matchAll(/<script(?: [^>]*)?>([\s\S]*?)<\/script>/g),
    ]
    assert.doesNotThrow(() => new Function(scripts.at(-1)[1]))
  } finally {
    await fs.rm(directory, { recursive: true, force: true })
  }
})
