// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.
import fs from 'node:fs/promises'
import path from 'node:path'

import { pathExists, run, sha256 } from './system.mjs'

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

const REVIEW_LABELS = {
  'native/smoke-browser-chrome-1280x800.png': '浏览器工具栏与指纹检测',
  'page/smoke-facebook-1280x800.png': 'Facebook 页面',
  'page/smoke-fingerprint-guide-1280x800.png': '指纹浏览器使用指南',
  'page/smoke-fingerprint-probe-1280x800.png': '指纹自动化探针',
  'page/smoke-fingerprint-test-1280x800.png': '指纹检测结果页',
  'page/smoke-google-1280x800.png': 'Google 页面',
  'page/smoke-new-tab-1280x800.png': '新标签页',
  'page/smoke-settings-1280x800.png': 'Brave 设置页',
  'page/smoke-wikipedia-1280x800.png': 'Wikipedia 页面',
}

function reviewLabel(baselineKey) {
  return (
    REVIEW_LABELS[baselineKey]
    || path.basename(baselineKey, path.extname(baselineKey))
  )
}

function reviewChecklist(baselineKey) {
  if (baselineKey.startsWith('native/')) {
    return '检查工具栏图标颜色、按钮文字、间距和页面边界。'
  }
  if (baselineKey.includes('fingerprint-guide')) {
    return '检查中文文案、步骤顺序、按钮和状态说明。'
  }
  if (baselineKey.includes('fingerprint-test')) {
    return '检查检测结果、表格、匹配状态和中文按钮。'
  }
  if (baselineKey.includes('fingerprint-probe')) {
    return '检查探针内容完整，没有空白、截断或异常覆盖。'
  }
  return '检查页面是否完整加载，没有空白、截断、重叠或异常颜色。'
}

function htmlReviewPage({ artifacts, items, manifest }) {
  const payload = JSON.stringify({
    items,
    manifest,
    storageKey: [
      'fingerprint-visual-review',
      artifacts.libchrome.source.sha256,
      artifacts.resources.source.sha256,
      artifacts.resources.chromiumSource.sha256,
    ].join(':'),
  }).replaceAll('<', '\\u003c')
  return `<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>指纹浏览器截图审批</title>
  <style>
    :root {
      color-scheme: light;
      --bg: #f5f7fa;
      --surface: #ffffff;
      --surface-muted: #eef1f5;
      --text: #17202a;
      --muted: #5b6673;
      --line: #d8dee7;
      --accent: #1769e0;
      --accent-hover: #0d55bd;
      --pass: #147a45;
      --pass-bg: #e7f6ed;
      --fail: #b42318;
      --fail-bg: #fdeceb;
      --pending: #8a5a00;
      --pending-bg: #fff4d6;
      font-family: -apple-system, BlinkMacSystemFont, "PingFang SC", "Microsoft YaHei", sans-serif;
    }
    * { box-sizing: border-box; }
    body { margin: 0; background: var(--bg); color: var(--text); }
    button, textarea { font: inherit; }
    button { cursor: pointer; }
    button:focus-visible, textarea:focus-visible { outline: 3px solid #8bb8f7; outline-offset: 2px; }
    .page-header { padding: 32px max(24px, calc((100vw - 1440px) / 2)); background: var(--surface); border-bottom: 1px solid var(--line); }
    .page-header h1 { margin: 0 0 8px; font-size: 28px; line-height: 1.25; letter-spacing: 0; }
    .page-header p { margin: 0; color: var(--muted); font-size: 15px; }
    .toolbar { position: sticky; top: 0; z-index: 20; display: flex; flex-wrap: wrap; align-items: center; gap: 12px; padding: 14px max(24px, calc((100vw - 1440px) / 2)); background: rgba(255, 255, 255, .96); border-bottom: 1px solid var(--line); backdrop-filter: blur(10px); }
    .summary { display: flex; gap: 8px; margin-right: auto; }
    .summary span { min-width: 78px; padding: 7px 10px; border-radius: 6px; background: var(--surface-muted); color: var(--muted); text-align: center; font-size: 13px; font-weight: 600; }
    .filters { display: inline-flex; padding: 3px; border: 1px solid var(--line); border-radius: 6px; background: var(--surface-muted); }
    .filters button { min-height: 34px; padding: 0 12px; border: 0; border-radius: 4px; background: transparent; color: var(--muted); }
    .filters button[aria-pressed="true"] { background: var(--surface); color: var(--text); box-shadow: 0 1px 2px rgba(16, 24, 40, .12); }
    .command { min-height: 40px; padding: 0 14px; border: 1px solid var(--line); border-radius: 6px; background: var(--surface); color: var(--text); font-weight: 600; }
    .command:hover { background: var(--surface-muted); }
    .command.primary { border-color: var(--accent); background: var(--accent); color: white; }
    .command.primary:hover { background: var(--accent-hover); }
    main { width: min(1440px, calc(100% - 48px)); margin: 24px auto 64px; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(min(100%, 420px), 1fr)); gap: 18px; }
    .review-card { overflow: hidden; border: 1px solid var(--line); border-radius: 8px; background: var(--surface); box-shadow: 0 2px 10px rgba(16, 24, 40, .05); }
    .review-card[data-status="PASS"] { border-color: #82c89f; }
    .review-card[data-status="FAIL"] { border-color: #e5a09a; }
    .preview { display: block; width: 100%; padding: 0; border: 0; border-bottom: 1px solid var(--line); background: #111; text-align: left; }
    .preview img { display: block; width: 100%; aspect-ratio: 16 / 10; object-fit: contain; }
    .card-body { padding: 16px; }
    .card-heading { display: flex; align-items: flex-start; gap: 12px; }
    .card-heading h2 { margin: 0; font-size: 18px; line-height: 1.35; letter-spacing: 0; }
    .status { flex: none; margin-left: auto; padding: 4px 8px; border-radius: 999px; background: var(--pending-bg); color: var(--pending); font-size: 12px; font-weight: 700; }
    .review-card[data-status="PASS"] .status { background: var(--pass-bg); color: var(--pass); }
    .review-card[data-status="FAIL"] .status { background: var(--fail-bg); color: var(--fail); }
    .checklist { min-height: 42px; margin: 10px 0 12px; color: var(--muted); font-size: 14px; line-height: 1.5; }
    .key { overflow: hidden; margin: 0 0 14px; color: #788493; font-family: ui-monospace, SFMono-Regular, Menlo, monospace; font-size: 11px; text-overflow: ellipsis; white-space: nowrap; }
    .decision { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
    .decision button { min-height: 42px; border: 1px solid var(--line); border-radius: 6px; background: var(--surface); font-weight: 700; }
    .decision .pass:hover, .decision .pass[aria-pressed="true"] { border-color: var(--pass); background: var(--pass-bg); color: var(--pass); }
    .decision .fail:hover, .decision .fail[aria-pressed="true"] { border-color: var(--fail); background: var(--fail-bg); color: var(--fail); }
    textarea { width: 100%; min-height: 72px; margin-top: 10px; padding: 10px 12px; resize: vertical; border: 1px solid var(--line); border-radius: 6px; background: var(--surface); color: var(--text); line-height: 1.45; }
    textarea::placeholder { color: #8994a1; }
    .empty { padding: 72px 24px; color: var(--muted); text-align: center; }
    dialog { width: 100vw; max-width: none; height: 100vh; max-height: none; margin: 0; padding: 0; border: 0; background: rgba(10, 13, 18, .96); color: white; }
    dialog::backdrop { background: rgba(10, 13, 18, .96); }
    .lightbox { display: grid; grid-template-rows: 64px minmax(0, 1fr); width: 100%; height: 100%; }
    .lightbox-header { display: flex; align-items: center; gap: 12px; padding: 0 18px; border-bottom: 1px solid rgba(255,255,255,.15); }
    .lightbox-title { overflow: hidden; margin-right: auto; font-size: 16px; font-weight: 700; text-overflow: ellipsis; white-space: nowrap; }
    .icon-button { width: 42px; height: 42px; border: 1px solid rgba(255,255,255,.25); border-radius: 6px; background: rgba(255,255,255,.08); color: white; font-size: 28px; line-height: 1; }
    .icon-button:hover { background: rgba(255,255,255,.16); }
    .lightbox-stage { position: relative; display: grid; place-items: center; min-height: 0; padding: 24px 72px; }
    .lightbox-stage img { display: block; max-width: 100%; max-height: 100%; object-fit: contain; box-shadow: 0 12px 40px rgba(0,0,0,.45); }
    .lightbox-stage .previous, .lightbox-stage .next { position: absolute; top: 50%; transform: translateY(-50%); }
    .lightbox-stage .previous { left: 16px; }
    .lightbox-stage .next { right: 16px; }
    .hidden-input { display: none; }
    @media (max-width: 760px) {
      .page-header { padding: 24px 16px; }
      .toolbar { padding: 12px 16px; }
      .summary { width: 100%; margin-right: 0; }
      .summary span { flex: 1; min-width: 0; }
      .filters { order: 3; width: 100%; }
      .filters button { flex: 1; padding: 0 6px; }
      main { width: calc(100% - 24px); margin-top: 12px; }
      .grid { grid-template-columns: 1fr; }
      .lightbox-stage { padding: 16px 50px; }
      .lightbox-stage .previous { left: 4px; }
      .lightbox-stage .next { right: 4px; }
    }
  </style>
</head>
<body>
  <header class="page-header">
    <h1>指纹浏览器截图审批</h1>
    <p id="build-summary"></p>
  </header>
  <section class="toolbar" aria-label="审批工具栏">
    <div class="summary" aria-live="polite">
      <span id="total-count">总计 0</span>
      <span id="pass-count">通过 0</span>
      <span id="fail-count">未通过 0</span>
      <span id="pending-count">待审批 0</span>
    </div>
    <div class="filters" aria-label="筛选审批状态">
      <button type="button" data-filter="ALL" aria-pressed="true">全部</button>
      <button type="button" data-filter="PENDING" aria-pressed="false">待审批</button>
      <button type="button" data-filter="PASS" aria-pressed="false">已通过</button>
      <button type="button" data-filter="FAIL" aria-pressed="false">未通过</button>
    </div>
    <button class="command" id="approve-all" type="button">全部标记通过</button>
    <button class="command" id="import-review" type="button">导入结果</button>
    <button class="command primary" id="export-review" type="button">导出审批结果</button>
    <input class="hidden-input" id="review-file" type="file" accept="application/json,.json">
  </section>
  <main>
    <section class="grid" id="review-grid" aria-live="polite"></section>
    <p class="empty" id="empty-state" hidden>当前筛选条件下没有截图。</p>
  </main>
  <dialog id="lightbox" aria-label="截图放大预览">
    <div class="lightbox">
      <div class="lightbox-header">
        <div class="lightbox-title" id="lightbox-title"></div>
        <button class="icon-button" id="lightbox-close" type="button" title="关闭" aria-label="关闭">×</button>
      </div>
      <div class="lightbox-stage">
        <button class="icon-button previous" id="lightbox-previous" type="button" title="上一张" aria-label="上一张">‹</button>
        <img id="lightbox-image" alt="">
        <button class="icon-button next" id="lightbox-next" type="button" title="下一张" aria-label="下一张">›</button>
      </div>
    </div>
  </dialog>
  <script id="review-data" type="application/json">${payload}</script>
  <script>
    (() => {
      const data = JSON.parse(document.getElementById('review-data').textContent)
      const defaultPassReason = '界面显示正常，未发现截断、重叠、颜色或可读性问题。'
      const state = Object.fromEntries(data.items.map((item) => [
        item.baselineKey,
        { reason: '', screenshotSha256: item.screenshotSha256, status: '' },
      ]))
      let filter = 'ALL'
      let lightboxIndex = 0

      function loadSaved() {
        try {
          const saved = JSON.parse(localStorage.getItem(data.storageKey) || 'null')
          for (const item of data.items) {
            const review = saved?.[item.baselineKey]
            if (review?.screenshotSha256 === item.screenshotSha256) {
              state[item.baselineKey] = review
            }
          }
        } catch {}
      }

      function persist() {
        try {
          localStorage.setItem(data.storageKey, JSON.stringify(state))
        } catch {}
      }

      function statusText(status) {
        if (status === 'PASS') return '已通过'
        if (status === 'FAIL') return '未通过'
        return '待审批'
      }

      function visible(item) {
        const status = state[item.baselineKey].status || 'PENDING'
        return filter === 'ALL' || filter === status
      }

      function setStatus(key, status) {
        state[key].status = status
        if (status === 'PASS' && !state[key].reason.trim()) {
          state[key].reason = defaultPassReason
        }
        if (status === 'FAIL' && state[key].reason === defaultPassReason) {
          state[key].reason = ''
        }
        persist()
        render()
        if (status === 'FAIL') {
          document.querySelector('[data-note="' + CSS.escape(key) + '"]')?.focus()
        }
      }

      function createButton(label, className, pressed, handler) {
        const button = document.createElement('button')
        button.type = 'button'
        button.className = className
        button.textContent = label
        button.setAttribute('aria-pressed', String(pressed))
        button.addEventListener('click', handler)
        return button
      }

      function openLightbox(index) {
        lightboxIndex = index
        const item = data.items[index]
        document.getElementById('lightbox-title').textContent = item.label
        const image = document.getElementById('lightbox-image')
        image.src = item.src
        image.alt = item.label
        const dialog = document.getElementById('lightbox')
        if (!dialog.open) dialog.showModal()
      }

      function moveLightbox(offset) {
        lightboxIndex = (lightboxIndex + offset + data.items.length) % data.items.length
        openLightbox(lightboxIndex)
      }

      function render() {
        const grid = document.getElementById('review-grid')
        grid.replaceChildren()
        const counts = { PASS: 0, FAIL: 0, PENDING: 0 }
        data.items.forEach((item, index) => {
          const review = state[item.baselineKey]
          counts[review.status || 'PENDING'] += 1
          if (!visible(item)) return

          const card = document.createElement('article')
          card.className = 'review-card'
          card.dataset.status = review.status || 'PENDING'

          const preview = document.createElement('button')
          preview.type = 'button'
          preview.className = 'preview'
          preview.title = '放大查看'
          preview.setAttribute('aria-label', '放大查看：' + item.label)
          preview.addEventListener('click', () => openLightbox(index))
          const image = document.createElement('img')
          image.src = item.src
          image.alt = item.label
          image.loading = 'lazy'
          preview.append(image)

          const body = document.createElement('div')
          body.className = 'card-body'
          const heading = document.createElement('div')
          heading.className = 'card-heading'
          const title = document.createElement('h2')
          title.textContent = item.label
          const badge = document.createElement('span')
          badge.className = 'status'
          badge.textContent = statusText(review.status)
          heading.append(title, badge)

          const checklist = document.createElement('p')
          checklist.className = 'checklist'
          checklist.textContent = item.checklist
          const key = document.createElement('p')
          key.className = 'key'
          key.title = item.baselineKey
          key.textContent = item.baselineKey

          const decision = document.createElement('div')
          decision.className = 'decision'
          decision.append(
            createButton('通过', 'pass', review.status === 'PASS', () => setStatus(item.baselineKey, 'PASS')),
            createButton('未通过', 'fail', review.status === 'FAIL', () => setStatus(item.baselineKey, 'FAIL')),
          )

          const note = document.createElement('textarea')
          note.dataset.note = item.baselineKey
          note.placeholder = review.status === 'FAIL' ? '请填写需要修改的问题' : '审批备注（可修改）'
          note.value = review.reason
          note.addEventListener('input', () => {
            state[item.baselineKey].reason = note.value
            persist()
          })

          body.append(heading, checklist, key, decision, note)
          card.append(preview, body)
          grid.append(card)
        })

        document.getElementById('total-count').textContent = '总计 ' + data.items.length
        document.getElementById('pass-count').textContent = '通过 ' + counts.PASS
        document.getElementById('fail-count').textContent = '未通过 ' + counts.FAIL
        document.getElementById('pending-count').textContent = '待审批 ' + counts.PENDING
        document.getElementById('empty-state').hidden = grid.childElementCount > 0
      }

      function exportReview() {
        const incomplete = data.items.filter((item) => {
          const review = state[item.baselineKey]
          return !['PASS', 'FAIL'].includes(review.status) || !review.reason.trim()
        })
        if (incomplete.length > 0) {
          alert('还有 ' + incomplete.length + ' 张截图未完成审批或缺少备注。')
          return
        }
        const output = { ...data.manifest, reviews: state }
        const blob = new Blob([JSON.stringify(output, null, 2) + '\\n'], { type: 'application/json' })
        const link = document.createElement('a')
        link.href = URL.createObjectURL(blob)
        link.download = 'visual-review.json'
        link.click()
        setTimeout(() => URL.revokeObjectURL(link.href), 1000)
      }

      async function importReview(file) {
        const imported = JSON.parse(await file.text())
        if (
          imported.libchromeSha256 !== data.manifest.libchromeSha256
          || imported.resourcesSha256 !== data.manifest.resourcesSha256
          || imported.chromiumResourcesSha256 !== data.manifest.chromiumResourcesSha256
        ) {
          alert('审批结果不属于当前构建。')
          return
        }
        for (const item of data.items) {
          const review = imported.reviews?.[item.baselineKey]
          if (review?.screenshotSha256 === item.screenshotSha256) {
            state[item.baselineKey] = review
          }
        }
        persist()
        render()
      }

      loadSaved()
      document.getElementById('build-summary').textContent =
        data.items.length + ' 张唯一截图 · 构建 ' + data.manifest.libchromeSha256.slice(0, 12)
      document.querySelectorAll('[data-filter]').forEach((button) => {
        button.addEventListener('click', () => {
          filter = button.dataset.filter
          document.querySelectorAll('[data-filter]').forEach((candidate) => {
            candidate.setAttribute('aria-pressed', String(candidate === button))
          })
          render()
        })
      })
      document.getElementById('approve-all').addEventListener('click', () => {
        for (const item of data.items) {
          if (!state[item.baselineKey].status) {
            state[item.baselineKey].status = 'PASS'
            state[item.baselineKey].reason = defaultPassReason
          }
        }
        persist()
        render()
      })
      document.getElementById('export-review').addEventListener('click', exportReview)
      document.getElementById('import-review').addEventListener('click', () => {
        document.getElementById('review-file').click()
      })
      document.getElementById('review-file').addEventListener('change', async (event) => {
        const file = event.target.files?.[0]
        if (file) await importReview(file)
        event.target.value = ''
      })
      document.getElementById('lightbox-close').addEventListener('click', () => {
        document.getElementById('lightbox').close()
      })
      document.getElementById('lightbox-previous').addEventListener('click', () => moveLightbox(-1))
      document.getElementById('lightbox-next').addEventListener('click', () => moveLightbox(1))
      document.getElementById('lightbox').addEventListener('click', (event) => {
        if (event.target.id === 'lightbox') event.currentTarget.close()
      })
      document.addEventListener('keydown', (event) => {
        if (!document.getElementById('lightbox').open) return
        if (event.key === 'ArrowLeft') moveLightbox(-1)
        if (event.key === 'ArrowRight') moveLightbox(1)
      })
      render()
    })()
  </script>
</body>
</html>
`
}

export async function writeVisualReviewBundle({ analyses, artifacts, runDir }) {
  const candidateDir = path.join(runDir, 'screenshots', 'candidate-baselines')
  const reviews = {}
  const items = []
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
    reviews[analysis.baselineKey] = {
      reason: '',
      screenshotSha256: await sha256(analysis.actual),
      status: '',
    }
    items.push({
      baselineKey: analysis.baselineKey,
      checklist: reviewChecklist(analysis.baselineKey),
      label: reviewLabel(analysis.baselineKey),
      screenshotSha256: reviews[analysis.baselineKey].screenshotSha256,
      src: path
        .join('screenshots', 'candidate-baselines', analysis.baselineKey)
        .split(path.sep)
        .join('/'),
    })
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
  const htmlFile = path.join(runDir, 'visual-review.html')
  const manifest = {
    chromiumResourcesSha256: artifacts.resources.chromiumSource.sha256,
    libchromeSha256: artifacts.libchrome.source.sha256,
    resourcesSha256: artifacts.resources.source.sha256,
    reviews,
  }
  await fs.writeFile(
    manifestFile,
    `${JSON.stringify(manifest, null, 2)}\n`,
  )
  await fs.writeFile(galleryFile, `${gallery.join('\n')}\n`)
  await fs.writeFile(htmlFile, htmlReviewPage({ artifacts, items, manifest }))
  return { candidateDir, galleryFile, htmlFile, manifestFile }
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
  const screenshotHashes = await Promise.all(
    analyses.map((analysis) => sha256(analysis.actual)),
  )
  const changed = reviews.filter(
    (review, index) => review.screenshotSha256 !== screenshotHashes[index],
  )
  if (changed.length > 0) {
    return {
      changed: changed.map((review) => review.baselineKey),
      reason: `${changed.length} screenshot hashes do not match the review`,
      reviews,
      status: 'BLOCKED',
    }
  }
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
