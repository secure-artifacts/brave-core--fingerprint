// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.
import fs from 'node:fs/promises'
import path from 'node:path'

function xml(value) {
  return String(value)
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&apos;')
}

function redact(value) {
  if (Array.isArray(value)) {
    return value.map(redact)
  }
  if (!value || typeof value !== 'object') {
    return value
  }
  return Object.fromEntries(
    Object.entries(value).map(([key, item]) => [
      key,
      /password|authorization|credential|username|secret|token/i.test(key)
        ? '<redacted>'
        : redact(item),
    ]),
  )
}

export function finalStatus(scenarios) {
  if (scenarios.some((scenario) => scenario.status === 'FAIL')) {
    return 'FAIL'
  }
  if (scenarios.some((scenario) => scenario.status === 'BLOCKED')) {
    return 'BLOCKED'
  }
  return 'PASS'
}

export async function writeReports(report, runDir) {
  report.status = finalStatus(report.scenarios)
  report.finishedAt = new Date().toISOString()
  const safeReport = redact(report)
  await fs.writeFile(
    path.join(runDir, 'report.json'),
    `${JSON.stringify(safeReport, null, 2)}\n`,
  )

  const lines = [
    `# Brave Fingerprint Browser QA: ${safeReport.status}`,
    '',
    `- Run: \`${safeReport.runId}\``,
    `- Mode: \`${safeReport.mode}\``,
    `- Started: ${safeReport.startedAt}`,
    `- Finished: ${safeReport.finishedAt}`,
    `- App: \`${safeReport.config.app}\``,
    `- Profile root: \`${safeReport.profileRoot}\``,
    '',
    '## Scenarios',
    '',
    '| ID | Status | Duration | Reason |',
    '| --- | --- | ---: | --- |',
  ]
  for (const scenario of safeReport.scenarios) {
    const reason = String(scenario.reason || '').replaceAll('|', '\\|')
    lines.push(
      `| ${scenario.id} | ${scenario.status} | ${scenario.durationMs || 0} ms | ${reason} |`,
    )
  }
  if (safeReport.artifacts) {
    lines.push('', '## Artifacts', '')
    lines.push(`- Dylibs: ${safeReport.artifacts.dylibs.count} matched`)
    lines.push(
      `- App/Framework/Helper executables: ${safeReport.artifacts.baseApp.source.count} matched`,
    )
    lines.push(
      `- Build source manifest: \`${safeReport.artifacts.buildManifest.file}\``,
    )
    lines.push(
      `- libchrome SHA-256: \`${safeReport.artifacts.libchrome.source.sha256}\``,
    )
    lines.push(
      `- resources SHA-256: \`${safeReport.artifacts.resources.source.sha256}\``,
    )
    lines.push(
      `- Chromium resources SHA-256: \`${safeReport.artifacts.resources.chromiumSource.sha256}\``,
    )
    lines.push(
      `- Scaled resource packs: ${safeReport.artifacts.resources.scaled.count} matched`,
    )
    lines.push(
      `- Locale packs: ${safeReport.artifacts.resources.locales.count} matched`,
    )
    lines.push(
      `- Locale set SHA-256: \`${safeReport.artifacts.resources.locales.digest}\``,
    )
    lines.push(
      `- Codesign: ${safeReport.artifacts.signature.pass ? 'PASS' : 'FAIL'}`,
    )
  }
  const analyses = safeReport.scenarios.flatMap(
    (scenario) => scenario.analyses || [],
  )
  if (analyses.length > 0) {
    lines.push('', '## Screenshot Analysis', '')
    lines.push('| Screenshot | Status | Baseline | Reason |')
    lines.push('| --- | --- | --- | --- |')
    for (const analysis of analyses) {
      const reason = String(analysis.reason || '').replaceAll('|', '\\|')
      lines.push(
        `| ${analysis.baselineKey} | ${analysis.pass ? 'PASS' : 'FAIL'} | ${analysis.baselineStatus} | ${reason} |`,
      )
    }
  }
  const reviews = safeReport.scenarios.flatMap(
    (scenario) => scenario.review?.reviews || [],
  )
  if (reviews.length > 0) {
    lines.push('', '## Human Visual Review', '')
    lines.push('| Screenshot | Status | Reason |')
    lines.push('| --- | --- | --- |')
    for (const review of reviews) {
      const reason = String(review.reason || '').replaceAll('|', '\\|')
      lines.push(`| ${review.baselineKey} | ${review.status} | ${reason} |`)
    }
  }
  const reviewBundles = safeReport.scenarios
    .map((scenario) => scenario.reviewBundle)
    .filter(Boolean)
  if (reviewBundles.length > 0) {
    const reviewBundle = reviewBundles.at(-1)
    lines.push('', '## Visual Review Bundle', '')
    lines.push(`- Approval page: \`${reviewBundle.htmlFile}\``)
    lines.push(`- Gallery: \`${reviewBundle.galleryFile}\``)
    lines.push(`- Candidate baselines: \`${reviewBundle.candidateDir}\``)
    lines.push(`- Review manifest template: \`${reviewBundle.manifestFile}\``)
  }
  lines.push('', '## Evidence', '')
  lines.push(`- Logs: \`${path.join(runDir, 'logs')}\``)
  lines.push(`- Crashes: \`${path.join(runDir, 'crashes')}\``)
  lines.push(`- Screenshots: \`${path.join(runDir, 'screenshots')}\``)
  await fs.writeFile(path.join(runDir, 'report.md'), `${lines.join('\n')}\n`)

  const failures = safeReport.scenarios.filter(
    (scenario) => scenario.status === 'FAIL',
  ).length
  const skipped = safeReport.scenarios.filter(
    (scenario) => scenario.status === 'BLOCKED',
  ).length
  const duration =
    safeReport.scenarios.reduce(
      (total, scenario) => total + (scenario.durationMs || 0),
      0,
    ) / 1000
  const cases = safeReport.scenarios.map((scenario) => {
    const common = `name="${xml(scenario.id)}" time="${(scenario.durationMs || 0) / 1000}"`
    if (scenario.status === 'FAIL') {
      return `  <testcase ${common}><failure message="${xml(scenario.reason || 'failed')}"/></testcase>`
    }
    if (scenario.status === 'BLOCKED') {
      return `  <testcase ${common}><skipped message="${xml(scenario.reason || 'blocked')}"/></testcase>`
    }
    return `  <testcase ${common}/>`
  })
  const junit = [
    '<?xml version="1.0" encoding="UTF-8"?>',
    `<testsuite name="brave-fingerprint-browser-${xml(safeReport.mode)}" tests="${cases.length}" failures="${failures}" skipped="${skipped}" time="${duration}">`,
    ...cases,
    '</testsuite>',
    '',
  ].join('\n')
  await fs.writeFile(path.join(runDir, 'junit.xml'), junit)
  return safeReport
}

export async function runScenario(report, id, operation) {
  const startedAt = Date.now()
  const scenario = {
    crashArtifacts: [],
    errors: [],
    id,
    processes: [],
    screenshots: [],
    status: 'PASS',
    url: null,
  }
  try {
    const result = await operation()
    if (result && typeof result === 'object') {
      const { status, ...details } = result
      Object.assign(scenario, details)
      if (['PASS', 'FAIL', 'BLOCKED'].includes(status)) {
        scenario.status = status
      } else if (status !== undefined) {
        scenario.resultStatus = status
      }
    }
  } catch (error) {
    scenario.status = 'FAIL'
    scenario.reason = error.message
    scenario.error = {
      details: error.details,
      stack: error.stack,
    }
    scenario.errors = [scenario.error]
    scenario.crashArtifacts =
      error.crashArtifacts || error.details?.crashArtifacts || []
  }
  scenario.durationMs = Date.now() - startedAt
  report.scenarios.push(scenario)
  return scenario
}
