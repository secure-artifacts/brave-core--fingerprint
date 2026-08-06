#!/usr/bin/env node

import fs from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'

import { startQaSession } from './lib/browser.mjs'

function parseArgs(argv) {
  const options = {
    app: '',
    iterations: 3,
    maxEventMs: 200,
    maxLongTaskMs: 1500,
    maxSearchMs: 5000,
    preSearchIdleMs: 500,
    query: 'language',
    warmups: 1,
  }

  for (let index = 0; index < argv.length; index += 1) {
    const value = argv[index + 1]
    switch (argv[index]) {
      case '--app':
        options.app = value
        index += 1
        break
      case '--iterations':
        options.iterations = Number(value)
        index += 1
        break
      case '--max-event-ms':
        options.maxEventMs = Number(value)
        index += 1
        break
      case '--max-long-task-ms':
        options.maxLongTaskMs = Number(value)
        index += 1
        break
      case '--max-search-ms':
        options.maxSearchMs = Number(value)
        index += 1
        break
      case '--pre-search-idle-ms':
        options.preSearchIdleMs = Number(value)
        index += 1
        break
      case '--query':
        options.query = value
        index += 1
        break
      case '--warmups':
        options.warmups = Number(value)
        index += 1
        break
      default:
        throw new Error(`Unknown argument: ${argv[index]}`)
    }
  }

  if (!options.app) throw new Error('--app is required')
  for (const key of [
    'iterations',
    'maxEventMs',
    'maxLongTaskMs',
    'maxSearchMs',
    'preSearchIdleMs',
    'warmups',
  ]) {
    if (!Number.isFinite(options[key]) || options[key] < 0) {
      throw new Error(`Invalid --${key}: ${options[key]}`)
    }
  }
  if (options.iterations < 1 || options.query.length === 0) {
    throw new Error('At least one iteration and a non-empty query are required')
  }
  return options
}

function percentile(values, fraction) {
  if (values.length === 0) return 0
  const sorted = [...values].sort((left, right) => left - right)
  return sorted[Math.ceil(fraction * sorted.length) - 1]
}

async function installPerformanceObservers(page) {
  await page.evaluate(() => {
    const state = {
      eventDurations: [],
      lastMutationAt: performance.now(),
      longTasks: [],
    }
    globalThis.__settingsSearchPerf = state

    const roots = [document]
    for (let index = 0; index < roots.length; index += 1) {
      for (const element of roots[index].querySelectorAll('*')) {
        if (element.shadowRoot) roots.push(element.shadowRoot)
      }
    }
    for (const root of roots) {
      new MutationObserver(() => {
        state.lastMutationAt = performance.now()
      }).observe(root, {
        attributes: true,
        childList: true,
        characterData: true,
        subtree: true,
      })
    }

    try {
      new PerformanceObserver((list) => {
        for (const entry of list.getEntries()) {
          state.longTasks.push(entry.duration)
        }
      }).observe({ buffered: true, type: 'longtask' })
    } catch {}

    try {
      new PerformanceObserver((list) => {
        for (const entry of list.getEntries()) {
          if (entry.name === 'keydown' || entry.name === 'input') {
            state.eventDurations.push(entry.duration)
          }
        }
      }).observe({ buffered: true, durationThreshold: 16, type: 'event' })
    } catch {}
  })
}

async function runIteration(page, query, preSearchIdleMs) {
  await page.goto('brave://settings/', { waitUntil: 'domcontentloaded' })
  const search = page.locator('input').filter({ visible: true }).first()
  await search.waitFor({ state: 'visible', timeout: 10000 })
  await page.waitForTimeout(preSearchIdleMs)
  await installPerformanceObservers(page)

  await search.click()
  const startedAt = performance.now()
  await page.keyboard.type(query, { delay: 20 })

  await page.waitForFunction(
    (expectedQuery) =>
      new URL(location.href).searchParams.get('search') === expectedQuery,
    query,
    { timeout: 10000 },
  )
  await page.waitForFunction(
    () => {
      const roots = [document]
      for (let index = 0; index < roots.length; index += 1) {
        for (const element of roots[index].querySelectorAll('*')) {
          if (element.shadowRoot) roots.push(element.shadowRoot)
        }
      }
      for (const root of roots) {
        const main = root.querySelector('settings-main')
        if (main) return main.toolbarSpinnerActive === false
      }
      return false
    },
    null,
    { polling: 25, timeout: 15000 },
  )
  await page.waitForFunction(
    () =>
      performance.now() - globalThis.__settingsSearchPerf.lastMutationAt >= 250,
    null,
    { polling: 25, timeout: 10000 },
  )
  const finishedAt = performance.now()

  const browserMetrics = await page.evaluate(() => {
    const roots = [document]
    for (let index = 0; index < roots.length; index += 1) {
      for (const element of roots[index].querySelectorAll('*')) {
        if (element.shadowRoot) roots.push(element.shadowRoot)
      }
    }
    const matches = []
    let totalElements = 0
    for (const root of roots) {
      totalElements += root.querySelectorAll('*').length
      for (const hit of root.querySelectorAll('.search-highlight-hit')) {
        matches.push(hit.textContent || '')
      }
    }
    return {
      eventDurations: globalThis.__settingsSearchPerf.eventDurations,
      longTasks: globalThis.__settingsSearchPerf.longTasks,
      matches: [...new Set(matches)],
      shadowRoots: roots.length - 1,
      totalElements,
    }
  })

  return {
    eventP95Ms: percentile(browserMetrics.eventDurations, 0.95),
    matches: browserMetrics.matches,
    maxLongTaskMs: Math.max(0, ...browserMetrics.longTasks),
    searchMs: finishedAt - startedAt,
    shadowRoots: browserMetrics.shadowRoots,
    totalElements: browserMetrics.totalElements,
    totalLongTaskMs: browserMetrics.longTasks.reduce(
      (total, duration) => total + duration,
      0,
    ),
  }
}

async function main() {
  const options = parseArgs(process.argv.slice(2))
  const runId = `${Date.now()}-${process.pid}`
  const root = path.join(
    os.tmpdir(),
    `fingerprint-browser-settings-perf-${runId}`,
  )
  let session

  try {
    session = await startQaSession({
      app: path.resolve(options.app),
      background: true,
      logDir: path.join(root, 'logs'),
      name: 'settings-search-perf',
      profilePath: path.join(root, 'profile'),
    })
    const page = session.context.pages()[0] || (await session.context.newPage())
    const results = []
    for (
      let index = 0;
      index < options.warmups + options.iterations;
      index += 1
    ) {
      const result = await runIteration(
        page,
        options.query,
        options.preSearchIdleMs,
      )
      if (index >= options.warmups) results.push(result)
    }

    const summary = {
      eventP95Ms: percentile(
        results.map((result) => result.eventP95Ms),
        0.95,
      ),
      maxLongTaskMs: Math.max(
        0,
        ...results.map((result) => result.maxLongTaskMs),
      ),
      searchP95Ms: percentile(
        results.map((result) => result.searchMs),
        0.95,
      ),
    }
    const failures = []
    const normalizedQuery = options.query.toLowerCase()
    if (
      results.some(
        (result) =>
          !result.matches.some((match) =>
            match.toLowerCase().includes(normalizedQuery),
          ),
      )
    ) {
      failures.push('expected search result was not highlighted')
    }
    if (summary.eventP95Ms > options.maxEventMs) {
      failures.push(
        `event p95 ${summary.eventP95Ms.toFixed(1)}ms > ${options.maxEventMs}ms`,
      )
    }
    if (summary.maxLongTaskMs > options.maxLongTaskMs) {
      failures.push(
        `long task ${summary.maxLongTaskMs.toFixed(1)}ms > ${options.maxLongTaskMs}ms`,
      )
    }
    if (summary.searchP95Ms > options.maxSearchMs) {
      failures.push(
        `search p95 ${summary.searchP95Ms.toFixed(1)}ms > ${options.maxSearchMs}ms`,
      )
    }

    const report = {
      limits: {
        maxEventMs: options.maxEventMs,
        maxLongTaskMs: options.maxLongTaskMs,
        maxSearchMs: options.maxSearchMs,
      },
      query: options.query,
      results,
      status: failures.length === 0 ? 'PASS' : 'FAIL',
      summary,
    }
    process.stdout.write(`${JSON.stringify(report, null, 2)}\n`)
    if (failures.length > 0) {
      process.stderr.write(
        `Settings search performance failed: ${failures.join('; ')}\n`,
      )
      process.exitCode = 1
    }
  } finally {
    await session?.close().catch(() => {})
    await fs.rm(root, { force: true, recursive: true }).catch(() => {})
  }
}

await main()
