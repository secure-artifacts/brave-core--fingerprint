import assert from 'node:assert/strict'
import fs from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'
import test from 'node:test'

import {analyzeScreenshot, loadHumanVisualReview} from '../lib/visual.mjs'
import {run} from '../lib/system.mjs'

test('analyzeScreenshot records SSIM and pixel comparison for approved baseline', async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'fp-qa-visual-'))
  const actual = path.join(directory, 'actual.png')
  const baselineDir = path.join(directory, 'baselines')
  const baseline = path.join(baselineDir, 'native', 'sample.png')
  try {
    await fs.mkdir(path.dirname(baseline), {recursive: true})
    await run('magick', [
      '-size', '32x32',
      'pattern:checkerboard',
      actual,
    ], {check: true})
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
    await fs.rm(directory, {recursive: true, force: true})
  }
})

test('analyzeScreenshot rejects a connected pure-red UI component', async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'fp-qa-red-'))
  const actual = path.join(directory, 'actual.png')
  try {
    await run('magick', [
      '-size', '64x64', 'xc:white',
      '-fill', 'red', '-draw', 'rectangle 8,8 31,31',
      actual,
    ], {check: true})
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
    await fs.rm(directory, {recursive: true, force: true})
  }
})

test('loadHumanVisualReview requires artifact-bound PASS/FAIL reasons', async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'fp-qa-review-'))
  const manifestFile = path.join(directory, 'review.json')
  const artifacts = {
    libchrome: {source: {sha256: 'lib'}},
    resources: {
      chromiumSource: {sha256: 'chromium-resources'},
      source: {sha256: 'resources'},
    },
  }
  const analyses = [{baselineKey: 'native/toolbar.png'}]
  try {
    await fs.writeFile(manifestFile, JSON.stringify({
      chromiumResourcesSha256: 'chromium-resources',
      libchromeSha256: 'lib',
      resourcesSha256: 'resources',
      reviews: {
        'native/toolbar.png': {reason: 'Icons are legible and correctly colored', status: 'PASS'},
      },
    }))
    const result = await loadHumanVisualReview({analyses, artifacts, manifestFile})
    assert.equal(result.status, 'PASS')
    assert.equal(result.reviews[0].reason, 'Icons are legible and correctly colored')
  } finally {
    await fs.rm(directory, {recursive: true, force: true})
  }
})
