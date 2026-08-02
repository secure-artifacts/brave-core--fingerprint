// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.
import fs from 'node:fs/promises'
import path from 'node:path'

import { pathExists, run } from './system.mjs'

async function imageStats(image) {
  const result = await run(
    'magick',
    [
      image,
      '-format',
      '%w %h %[fx:mean.r] %[fx:mean.g] %[fx:mean.b] %[fx:standard_deviation]',
      'info:',
    ],
    { check: true },
  )
  const [width, height, red, green, blue, standardDeviation] = result.stdout
    .trim()
    .split(/\s+/)
    .map(Number)
  return { blue, green, height, red, standardDeviation, width }
}

async function redPixelRatio(image) {
  const result = await run(
    'magick',
    [
      image,
      '-alpha',
      'on',
      '-colorspace',
      'sRGB',
      '-fx',
      '(a>0.5&&r>0.85&&g<0.18&&b<0.18)?1:0',
      '-format',
      '%[fx:mean]',
      'info:',
    ],
    { check: true },
  )
  return Number(result.stdout.trim())
}

async function redComponentStats(image) {
  const result = await run('magick', [
    image,
    '-alpha',
    'on',
    '-colorspace',
    'sRGB',
    '-fx',
    '(a>0.5&&r>0.85&&g<0.18&&b<0.18)?1:0',
    '-define',
    'connected-components:verbose=true',
    '-connected-components',
    '8',
    'null:',
  ])
  const components = `${result.stdout}\n${result.stderr}`
    .split('\n')
    .flatMap((line) => {
      const match = line.match(
        /^\s*\d+:\s+\S+\s+\S+\s+(\d+(?:\.\d+)?)\s+(?:gray|s?rgba?)\(([^)]*)\)/i,
      )
      if (!match) return []
      const channels = match[2].split(',').map(Number)
      if (
        !channels.some((channel) => Number.isFinite(channel) && channel > 0.5)
      )
        return []
      return [{ area: Number(match[1]), description: line.trim() }]
    })
  return {
    components,
    count: components.length,
    maxArea: Math.max(0, ...components.map((component) => component.area)),
  }
}

function normalizedMetric(result, name) {
  const output = `${result.stderr}\n${result.stdout}`.trim()
  const normalized = output.match(/\(([-+\deE.]+)\)/)
  const value = Number(normalized?.[1] || output.match(/[-+\d.eE]+/)?.[0])
  if (!Number.isFinite(value)) {
    throw new Error(`Could not parse ImageMagick ${name}: ${output}`)
  }
  return value
}

async function comparisonMetrics(actual, baseline, diffTarget, pixelCount) {
  await fs.mkdir(path.dirname(diffTarget), { recursive: true })
  const rmseResult = await run('magick', [
    'compare',
    '-metric',
    'RMSE',
    actual,
    baseline,
    diffTarget,
  ])
  const ssimResult = await run('magick', [
    'compare',
    '-metric',
    'SSIM',
    actual,
    baseline,
    'null:',
  ])
  const pixelResult = await run('magick', [
    'compare',
    '-fuzz',
    '5%',
    '-metric',
    'AE',
    actual,
    baseline,
    'null:',
  ])
  return {
    pixelDifferenceRatio: normalizedMetric(pixelResult, 'AE') / pixelCount,
    rmse: normalizedMetric(rmseResult, 'RMSE'),
    ssimDifference: normalizedMetric(ssimResult, 'SSIM'),
  }
}

async function applyMask(image, mask, target) {
  await fs.mkdir(path.dirname(target), { recursive: true })
  await run(
    'magick',
    [
      image,
      mask,
      '-alpha',
      'off',
      '-compose',
      'CopyOpacity',
      '-composite',
      target,
    ],
    { check: true },
  )
  return target
}

export async function analyzeScreenshot({
  actual,
  baselineKey = path.basename(actual),
  baselineDir,
  checkRed = false,
  diffDir,
  maxRedRatio = 0.015,
  maxRedComponentArea = 24,
  maxRmse = 0.06,
  maxPixelDifferenceRatio = 0.03,
  maxSsimDifference = 0.05,
}) {
  if (
    path.isAbsolute(baselineKey)
    || baselineKey.split(path.sep).includes('..')
  ) {
    throw new Error(`Invalid baseline key: ${baselineKey}`)
  }
  const baseline = path.join(baselineDir, baselineKey)
  const stats = await imageStats(actual)
  const checks = [
    {
      actual: stats.standardDeviation,
      limit: 0.002,
      name: 'nonblank-image-standard-deviation',
      pass: stats.standardDeviation >= 0.002,
    },
  ]
  let redRatio = null
  let redMask = null
  let redComponents = null
  if (checkRed) {
    const candidateRedMask = `${baseline}.red-mask.png`
    let redSource = actual
    if (await pathExists(candidateRedMask)) {
      redMask = candidateRedMask
      redSource = await applyMask(
        actual,
        redMask,
        path.join(diffDir, `${baselineKey}.red-masked.png`),
      )
    }
    redRatio = await redPixelRatio(redSource)
    redComponents = await redComponentStats(redSource)
    checks.push({
      actual: redRatio,
      limit: maxRedRatio,
      name: 'non-semantic-red-pixel-ratio',
      pass: redRatio <= maxRedRatio,
    })
    checks.push({
      actual: redComponents.maxArea,
      limit: maxRedComponentArea,
      name: 'non-semantic-red-connected-component-area',
      pass: redComponents.maxArea <= maxRedComponentArea,
    })
  }
  let baselineStatus = 'MISSING'
  let rmse = null
  let ssimDifference = null
  let pixelDifferenceRatio = null
  let diff = null
  let mask = null
  if (await pathExists(baseline)) {
    const baselineStats = await imageStats(baseline)
    if (
      baselineStats.width !== stats.width
      || baselineStats.height !== stats.height
    ) {
      checks.push({
        actual: `${stats.width}x${stats.height}`,
        expected: `${baselineStats.width}x${baselineStats.height}`,
        name: 'baseline-dimensions',
        pass: false,
      })
      baselineStatus = 'FAIL'
    } else {
      diff = path.join(diffDir, baselineKey)
      let comparedActual = actual
      let comparedBaseline = baseline
      const candidateMask = `${baseline}.mask.png`
      if (await pathExists(candidateMask)) {
        mask = candidateMask
        comparedActual = await applyMask(
          actual,
          mask,
          `${diff}.actual-masked.png`,
        )
        comparedBaseline = await applyMask(
          baseline,
          mask,
          `${diff}.baseline-masked.png`,
        )
      }
      const metrics = await comparisonMetrics(
        comparedActual,
        comparedBaseline,
        diff,
        stats.width * stats.height,
      )
      rmse = metrics.rmse
      ssimDifference = metrics.ssimDifference
      pixelDifferenceRatio = metrics.pixelDifferenceRatio
      checks.push(
        {
          actual: rmse,
          limit: maxRmse,
          name: 'baseline-rmse',
          pass: rmse <= maxRmse,
        },
        {
          actual: ssimDifference,
          limit: maxSsimDifference,
          name: 'baseline-ssim-difference',
          pass: ssimDifference <= maxSsimDifference,
        },
        {
          actual: pixelDifferenceRatio,
          limit: maxPixelDifferenceRatio,
          name: 'baseline-pixel-difference-ratio',
          pass: pixelDifferenceRatio <= maxPixelDifferenceRatio,
        },
      )
      baselineStatus = checks.slice(-3).every((check) => check.pass)
        ? 'PASS'
        : 'FAIL'
    }
  }
  return {
    actual,
    baseline,
    baselineStatus,
    baselineKey,
    checkRed,
    checks,
    diff,
    mask,
    pass: checks.every((check) => check.pass),
    reason: checks.every((check) => check.pass)
      ? 'All automated visual checks passed'
      : checks
          .filter((check) => !check.pass)
          .map((check) =>
            check.expected
              ? `${check.name} ${check.actual} != ${check.expected}`
              : `${check.name} ${check.actual} outside limit ${check.limit}`,
          )
          .join('; '),
    pixelDifferenceRatio,
    redMask,
    redComponents,
    redRatio,
    rmse,
    ssimDifference,
    stats,
  }
}

async function pngFiles(directory) {
  if (!(await pathExists(directory))) return []
  const files = []
  for (const entry of await fs.readdir(directory, { withFileTypes: true })) {
    const target = path.join(directory, entry.name)
    if (entry.isDirectory()) {
      files.push(...(await pngFiles(target)))
    } else if (entry.isFile() && entry.name.endsWith('.png')) {
      files.push(target)
    }
  }
  return files.sort()
}

export async function analyzeScreenshotTree({
  baselineDir,
  diffDir,
  nativeDir,
  pageDir,
  requireNativeBaselines,
}) {
  const groups = [
    { directory: pageDir, kind: 'page' },
    { directory: nativeDir, kind: 'native' },
  ]
  const analyses = []
  for (const group of groups) {
    for (const actual of await pngFiles(group.directory)) {
      const relative = path.relative(group.directory, actual)
      analyses.push(
        await analyzeScreenshot({
          actual,
          baselineDir,
          baselineKey: path.join(group.kind, relative),
          checkRed: group.kind === 'native',
          diffDir,
        }),
      )
    }
  }
  const failed = analyses.filter((analysis) => !analysis.pass)
  const missingRequiredBaselines = requireNativeBaselines
    ? analyses.filter((analysis) => analysis.baselineStatus === 'MISSING')
    : []
  return { analyses, failed, missingRequiredBaselines }
}

export async function writeVisualReviewBundle({ analyses, artifacts, runDir }) {
  const candidateDir = path.join(runDir, 'screenshots', 'candidate-baselines')
  const reviews = {}
  const gallery = [
    '# Brave Fingerprint Browser Visual Review',
    '',
    `- libchrome SHA-256: \`${artifacts.libchrome.source.sha256}\``,
    `- resources SHA-256: \`${artifacts.resources.source.sha256}\``,
    `- Chromium resources SHA-256: \`${artifacts.resources.chromiumSource.sha256}\``,
    '',
  ]
  for (const analysis of analyses) {
    const candidate = path.join(candidateDir, analysis.baselineKey)
    await fs.mkdir(path.dirname(candidate), { recursive: true })
    await fs.copyFile(analysis.actual, candidate)
    reviews[analysis.baselineKey] = { reason: '', status: '' }
    gallery.push(
      `## ${analysis.baselineKey}`,
      '',
      `- Automated: ${analysis.pass ? 'PASS' : 'FAIL'}`,
      `- Baseline: ${analysis.baselineStatus}`,
      `- Reason: ${analysis.reason}`,
      '',
      `![${analysis.baselineKey}](${path.relative(runDir, candidate)})`,
      '',
    )
  }
  const manifestFile = path.join(runDir, 'visual-review.template.json')
  const galleryFile = path.join(runDir, 'visual-review-gallery.md')
  await fs.writeFile(
    manifestFile,
    `${JSON.stringify(
      {
        chromiumResourcesSha256: artifacts.resources.chromiumSource.sha256,
        libchromeSha256: artifacts.libchrome.source.sha256,
        resourcesSha256: artifacts.resources.source.sha256,
        reviews,
      },
      null,
      2,
    )}\n`,
  )
  await fs.writeFile(galleryFile, `${gallery.join('\n')}\n`)
  return { candidateDir, galleryFile, manifestFile }
}

export async function loadHumanVisualReview({
  analyses,
  artifacts,
  manifestFile,
}) {
  if (!manifestFile) {
    return {
      missing: analyses.map((analysis) => analysis.baselineKey),
      reason: 'FP_QA_VISUAL_REVIEW_MANIFEST is required for Full/Soak delivery',
      status: 'BLOCKED',
    }
  }
  if (!(await pathExists(manifestFile))) {
    return {
      reason: `Visual review manifest does not exist: ${manifestFile}`,
      status: 'BLOCKED',
    }
  }
  const manifest = JSON.parse(await fs.readFile(manifestFile, 'utf8'))
  if (
    manifest.libchromeSha256 !== artifacts.libchrome.source.sha256
    || manifest.resourcesSha256 !== artifacts.resources.source.sha256
    || manifest.chromiumResourcesSha256
      !== artifacts.resources.chromiumSource.sha256
  ) {
    return {
      reason: 'Visual review manifest artifact hashes do not match',
      status: 'BLOCKED',
    }
  }
  const reviews = analyses.map((analysis) => ({
    baselineKey: analysis.baselineKey,
    ...(manifest.reviews?.[analysis.baselineKey] || {}),
  }))
  const missing = reviews.filter(
    (review) =>
      !['PASS', 'FAIL'].includes(review.status)
      || !String(review.reason || '').trim(),
  )
  if (missing.length > 0) {
    return {
      missing: missing.map((review) => review.baselineKey),
      reason: `${missing.length} screenshots lack a human PASS/FAIL reason`,
      reviews,
      status: 'BLOCKED',
    }
  }
  const failed = reviews.filter((review) => review.status === 'FAIL')
  return {
    failed,
    manifest: manifestFile,
    reason:
      failed.length > 0
        ? `${failed.length} screenshots failed human review`
        : null,
    reviews,
    status: failed.length > 0 ? 'FAIL' : 'PASS',
  }
}

function parseColor(value) {
  const rgb = value.match(
    /rgba?\(\s*(\d+(?:\.\d+)?)[,\s]+(\d+(?:\.\d+)?)[,\s]+(\d+(?:\.\d+)?)(?:\s*[,/]\s*(\d+(?:\.\d+)?))?/,
  )
  if (rgb) {
    return [...rgb.slice(1, 4).map(Number), Number(rgb[4] ?? 1)]
  }
  const srgb = value.match(
    /color\(srgb\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)(?:\s*\/\s*([\d.]+))?\)/,
  )
  return srgb
    ? [
        ...srgb.slice(1, 4).map((channel) => Number(channel) * 255),
        Number(srgb[4] ?? 1),
      ]
    : null
}

function channel(value) {
  const normalized = value / 255
  return normalized <= 0.04045
    ? normalized / 12.92
    : ((normalized + 0.055) / 1.055) ** 2.4
}

export function contrastRatio(foreground, background) {
  const fg = parseColor(foreground)
  const bg = parseColor(background)
  if (!fg || !bg) {
    return null
  }
  const composite = fg
    .slice(0, 3)
    .map((value, index) => value * fg[3] + bg[index] * (1 - fg[3]))
  const luminance = (color) =>
    0.2126 * channel(color[0])
    + 0.7152 * channel(color[1])
    + 0.0722 * channel(color[2])
  const light = Math.max(luminance(composite), luminance(bg))
  const dark = Math.min(luminance(composite), luminance(bg))
  return (light + 0.05) / (dark + 0.05)
}
