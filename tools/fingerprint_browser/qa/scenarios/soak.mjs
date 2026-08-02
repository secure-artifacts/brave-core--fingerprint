// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.
import path from 'node:path'

import { startQaSession } from '../lib/browser.mjs'
import { runLocalExtensionLifecycle } from '../lib/extensions.mjs'
import { loadProxyFixtures } from '../lib/fixtures.mjs'
import { setProfileProxy } from '../lib/profile.mjs'
import { runScenario } from '../lib/report.mjs'

function observedIp(body) {
  try {
    const parsed = JSON.parse(body)
    return parsed.ip || parsed.query || parsed.origin
  } catch {
    return body.trim()
  }
}

function eventFailures(events) {
  return [
    ...events.browserExits.map(
      (event) => `browser exited ${event.code ?? event.signal}`,
    ),
    ...events.crashes.map((event) => `renderer crash ${event.page}`),
    ...events.pageErrors.map((event) => `pageerror ${event.message}`),
    ...events.disconnected.map(() => 'CDP disconnected'),
    ...events.console
      .filter((event) =>
        /CHECK failed|FATAL|DYLD|Aw, Snap|crash/i.test(event.text),
      )
      .map((event) => `fatal console ${event.text}`),
  ]
}

function processSummary(processes) {
  const count = (type) =>
    processes.filter((process) =>
      type === 'browser'
        ? !process.command.includes('--type=')
        : process.command.includes(`--type=${type}`),
    ).length
  return {
    browser: count('browser'),
    gpu: count('gpu-process'),
    renderer: count('renderer'),
    rssKb: processes.reduce((sum, process) => sum + process.rssKb, 0),
    total: processes.length,
    utility: count('utility'),
  }
}

async function makePages(session, count, origin, offset) {
  const pages = session.context.pages()
  while (pages.length < count) {
    pages.push(await session.context.newPage())
  }
  for (let index = 0; index < count; index += 1) {
    await pages[index].goto(`${origin}/iframe.html?initial=${offset + index}`, {
      waitUntil: 'load',
    })
  }
  return pages.slice(0, count)
}

export async function runSoak({ config, dirs, probe, report, runId }) {
  let fixtures
  await runScenario(report, 'soak-required-fixtures', async () => {
    fixtures = await loadProxyFixtures(config.proxyFixtures)
    if (fixtures.status === 'BLOCKED') return fixtures
    if (!process.env.FP_QA_PRIMARY_EXTENSION_URL) {
      return {
        reason: 'FP_QA_PRIMARY_EXTENSION_URL is required before Soak',
        status: 'BLOCKED',
      }
    }
    return { status: 'PASS' }
  })
  if (fixtures?.status !== 'PASS' || !process.env.FP_QA_PRIMARY_EXTENSION_URL) {
    return
  }

  await runScenario(report, 'soak-60-minute-stability', async () => {
    const profiles = ['soak-a', 'soak-b', 'soak-c']
    const desiredPages = [7, 7, 6]
    const sessions = []
    const pagesBySession = []
    const allEvents = []
    const metrics = []
    const counters = {
      extensionCycles: 0,
      navigations: 0,
      profileCycles: 0,
      proxyToggles: 0,
    }

    const launch = async (index) => {
      const session = await startQaSession({
        app: config.app,
        logDir: dirs.logs,
        name: profiles[index],
        profilePath: `/tmp/fingerprint-browser-${runId}/${profiles[index]}`,
      })
      sessions[index] = session
      allEvents.push(session.events)
      return session
    }

    try {
      for (let index = 0; index < profiles.length; index += 1) {
        await launch(index)
        pagesBySession[index] = await makePages(
          sessions[index],
          desiredPages[index],
          probe.origin,
          desiredPages.slice(0, index).reduce((sum, value) => sum + value, 0),
        )
      }
      const fixtureDir = path.join(
        config.braveRoot,
        'tools',
        'fingerprint_browser',
        'qa',
        'fixtures',
        'mv3',
      )
      if (pagesBySession.flat().length !== 20) {
        throw new Error(`Expected 20 tabs, got ${pagesBySession.flat().length}`)
      }

      const startedAt = Date.now()
      const durationMs = config.durationMinutes * 60 * 1000
      const deadline = startedAt + durationMs
      const dueCount = (total) =>
        Math.min(
          total,
          Math.floor(((Date.now() - startedAt) / durationMs) * total) + 1,
        )
      let nextSample = startedAt
      while (
        Date.now() < deadline
        || counters.navigations < 200
        || counters.proxyToggles < 20
        || counters.profileCycles < 10
        || counters.extensionCycles < 10
      ) {
        if (counters.profileCycles < dueCount(10)) {
          const index = counters.profileCycles % sessions.length
          await sessions[index].close()
          await launch(index)
          pagesBySession[index] = await makePages(
            sessions[index],
            desiredPages[index],
            probe.origin,
            desiredPages.slice(0, index).reduce((sum, value) => sum + value, 0),
          )
          counters.profileCycles += 1
        }

        if (counters.proxyToggles < dueCount(20)) {
          const toggle = counters.proxyToggles
          const enabled = toggle % 2 === 0
          const fixture = toggle % 4 < 2 ? fixtures.http : fixtures.socks5
          const proxyPage = pagesBySession[0][0]
          const state = await setProfileProxy(
            proxyPage,
            enabled
              ? {
                  ...fixture,
                  enabled: true,
                }
              : { enabled: false },
          )
          if (enabled) {
            if (
              !state.enabled
              || state.state !== 'active'
              || state.egressIp !== fixture.expectedIp
            ) {
              throw Object.assign(
                new Error(`Soak proxy ${fixture.scheme} did not activate`),
                { details: state },
              )
            }
            const response = await proxyPage.goto(fixture.verifyUrl, {
              timeout: 60000,
              waitUntil: 'domcontentloaded',
            })
            const ip = response?.ok()
              ? observedIp(await proxyPage.locator('body').innerText())
              : null
            if (ip !== fixture.expectedIp) {
              throw new Error(
                `Soak proxy ${fixture.scheme} exit IP mismatch: ${ip}`,
              )
            }
          } else {
            const response = await proxyPage.goto(probe.origin, {
              timeout: 30000,
              waitUntil: 'domcontentloaded',
            })
            if (!response?.ok()) {
              throw new Error(
                `Direct navigation failed after disabling ${fixture.scheme}`,
              )
            }
          }
          counters.proxyToggles += 1
        }

        if (counters.extensionCycles < dueCount(10)) {
          const cycle = counters.extensionCycles
          await runLocalExtensionLifecycle({
            config,
            dirs,
            fixtureDir,
            probe,
            runId: `${runId}/extension-cycle-${cycle}`,
          })
          counters.extensionCycles += 1
        }

        const pages = pagesBySession.flat()
        if (pages.length !== 20) {
          throw new Error(`Soak tab count changed: ${pages.length}`)
        }
        const page = pages[counters.navigations % pages.length]
        if (counters.navigations % 2 === 0) {
          await page.goto(
            `${probe.origin}/iframe.html?navigation=${counters.navigations}`,
            { waitUntil: 'load' },
          )
        } else {
          await page.reload({ waitUntil: 'load' })
        }
        counters.navigations += 1

        if (Date.now() >= nextSample) {
          const sample = {
            cdp: [],
            processes: [],
            time: new Date().toISOString(),
          }
          for (const session of sessions) {
            const response = await fetch(
              `http://127.0.0.1:${session.port}/json/version`,
            )
            if (!response.ok) {
              throw new Error(`CDP health failed on port ${session.port}`)
            }
            sample.cdp.push({ ok: true, port: session.port })
            const processes = await session.processes()
            sample.processes.push(...processes)
            sample[`port${session.port}`] = processSummary(processes)
            if (
              processes.length === 0
              || processes.some((process) => process.state.startsWith('Z'))
            ) {
              throw new Error(
                `QA process health failed on port ${session.port}`,
              )
            }
          }
          sample.summary = processSummary(sample.processes)
          metrics.push(sample)
          nextSample += 60000
        }
        await new Promise((resolve) => setTimeout(resolve, 150))
      }

      const failures = allEvents.flatMap(eventFailures)
      if (failures.length > 0) {
        throw Object.assign(new Error(failures.join('; ')), {
          details: allEvents,
        })
      }
      if (
        counters.navigations < 200
        || counters.proxyToggles < 20
        || counters.profileCycles < 10
        || counters.extensionCycles < 10
      ) {
        throw new Error(`Soak counters incomplete: ${JSON.stringify(counters)}`)
      }
      return { counters, metrics, processes: metrics.at(-1)?.processes || [] }
    } finally {
      await Promise.all(
        sessions.filter(Boolean).map((session) => session.close()),
      )
    }
  })
}
