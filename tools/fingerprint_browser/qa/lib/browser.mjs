import fs from 'node:fs/promises'
import path from 'node:path'
import {spawn} from 'node:child_process'

import {contrastRatio} from './visual.mjs'
import {
  findFreePort,
  listProcesses,
  processesForProfile,
  stopProfileProcesses,
  waitForJson,
} from './system.mjs'

async function launchProcess({app, args, env, logDir, name}) {
  const executable = path.join(app, 'Contents', 'MacOS', 'Brave Browser Development')
  const stdoutFile = path.join(logDir, `${name}-stdout.log`)
  const stderrFile = path.join(logDir, `${name}-stderr.log`)
  const [stdout, stderr] = await Promise.all([
    fs.open(stdoutFile, 'a'),
    fs.open(stderrFile, 'a'),
  ])
  const child = spawn(executable, args, {
    detached: false,
    env: {...process.env, ...env},
    stdio: ['ignore', stdout.fd, stderr.fd],
  })
  const exit = new Promise((resolve, reject) => {
    child.once('error', reject)
    child.once('exit', (code, signal) => resolve({code, signal}))
  })
  return {
    child,
    closeLogs: async () => await Promise.all([stdout.close(), stderr.close()]),
    exit,
    stderrFile,
    stdoutFile,
  }
}

function attachPageMonitor(page, events) {
  if (page.__fingerprintQaMonitored) {
    return
  }
  page.__fingerprintQaMonitored = true
  page.on('console', message => {
    if (message.type() === 'error' || message.type() === 'warning') {
      events.console.push({
        location: message.location(),
        page: page.url(),
        text: message.text(),
        type: message.type(),
      })
    }
  })
  page.on('crash', () => events.crashes.push({page: page.url(), time: new Date().toISOString()}))
  page.on('pageerror', error => events.pageErrors.push({
    message: error.message,
    page: page.url(),
    stack: error.stack,
  }))
}

function mergePreferenceTree(target, updates) {
  for (const [key, value] of Object.entries(updates)) {
    if (value && typeof value === 'object' && !Array.isArray(value)) {
      const current = target[key]
      target[key] = mergePreferenceTree(
        current && typeof current === 'object' && !Array.isArray(current)
          ? current
          : {},
        value,
      )
    } else {
      target[key] = value
    }
  }
  return target
}

export async function seedProfilePreferences(
  profilePath,
  profileDirectory,
  updates,
) {
  const directory = path.join(profilePath, profileDirectory)
  const file = path.join(directory, 'Preferences')
  await fs.mkdir(directory, {recursive: true})
  let preferences = {}
  try {
    preferences = JSON.parse(await fs.readFile(file, 'utf8'))
  } catch (error) {
    if (error.code !== 'ENOENT') throw error
  }
  mergePreferenceTree(preferences, updates)
  await fs.writeFile(file, JSON.stringify(preferences))
}

export function qaBrowserArgs({
  extraArgs = [],
  language = 'en-US',
  platform = process.platform,
  port,
  profileDirectory,
  profilePath,
  testType = true,
}) {
  return [
    `--user-data-dir=${profilePath}`,
    `--profile-directory=${profileDirectory}`,
    `--remote-debugging-port=${port}`,
    '--remote-debugging-address=127.0.0.1',
    '--no-first-run',
    '--no-default-browser-check',
    '--disable-default-apps',
    '--disable-component-update',
    ...(language ? [`--lang=${language}`] : []),
    ...(testType ? ['--test-type'] : []),
    '--window-size=1280,800',
    ...(platform === 'darwin' ? ['--use-mock-keychain'] : []),
    ...extraArgs,
    'about:blank',
  ]
}

export async function startQaSession({
  app,
  env = {},
  extraArgs = [],
  language = 'en-US',
  logDir,
  name,
  profilePath,
  profileDirectory = 'Default',
  profilePreferences = null,
  testType = true,
}) {
  await fs.mkdir(profilePath, {recursive: true})
  await fs.mkdir(logDir, {recursive: true})
  await stopProfileProcesses(profilePath)
  if (profilePreferences) {
    await seedProfilePreferences(
      profilePath,
      profileDirectory,
      profilePreferences,
    )
  }
  const port = await findFreePort()
  const args = qaBrowserArgs({
    extraArgs,
    language,
    port,
    profileDirectory,
    profilePath,
    testType,
  })
  const process = await launchProcess({app, args, env, logDir, name})
  let version
  try {
    version = await Promise.race([
      waitForJson(`http://127.0.0.1:${port}/json/version`, 45000),
      process.exit.then(result => {
        throw new Error(`Brave exited before CDP connected: ${JSON.stringify(result)}`)
      }),
    ])
  } catch (error) {
    await stopProfileProcesses(profilePath).catch(() => {})
    await process.closeLogs()
    throw error
  }

  let browser
  let browserSession
  let context
  try {
    const {chromium} = await import('playwright-core')
    browser = await chromium.connectOverCDP(`http://127.0.0.1:${port}`)
    browserSession = await browser.newBrowserCDPSession()
    context = browser.contexts()[0]
    if (!context) {
      throw new Error('CDP connected without a browser context')
    }
  } catch (error) {
    await browser?.close().catch(() => {})
    await stopProfileProcesses(profilePath).catch(() => {})
    await process.closeLogs()
    throw error
  }
  const events = {
    browserExits: [],
    console: [],
    crashes: [],
    disconnected: [],
    pageErrors: [],
  }
  let closing = false
  process.exit.then(
    result => {
      if (!closing) {
        events.browserExits.push({...result, time: new Date().toISOString()})
      }
    },
    error => {
      if (!closing) {
        events.browserExits.push({error: error.message, time: new Date().toISOString()})
      }
    },
  )
  browser.on('disconnected', () => {
    if (!closing) {
      events.disconnected.push({time: new Date().toISOString()})
    }
  })
  for (const page of context.pages()) {
    await page.emulateMedia({colorScheme: null})
    attachPageMonitor(page, events)
  }
  context.on('page', page => attachPageMonitor(page, events))

  return {
    browser,
    context,
    events,
    port,
    process,
    profileDirectory,
    profilePath,
    version,
    async close() {
      closing = true
      try {
        await browserSession.send('Browser.close')
      } catch {
      }
      let exited = false
      await Promise.race([
        process.exit.then(() => {
          exited = true
        }),
        new Promise(resolve => setTimeout(resolve, 5000)),
      ])
      if (!exited) {
        await stopProfileProcesses(profilePath)
      }
      await browser.close().catch(() => {})
      await process.closeLogs()
    },
    async processes() {
      return processesForProfile(await listProcesses(), profilePath)
    },
  }
}

export async function startQaExtensionSession({
  app,
  env = {},
  extraArgs = [],
  logDir,
  name,
  profilePath,
  profileDirectory = 'Default',
}) {
  await fs.mkdir(profilePath, {recursive: true})
  await fs.mkdir(logDir, {recursive: true})
  await stopProfileProcesses(profilePath)
  const executablePath = path.join(
    app, 'Contents', 'MacOS', 'Brave Browser Development')
  const stdoutFile = path.join(logDir, `${name}-stdout.log`)
  const stderrFile = path.join(logDir, `${name}-stderr.log`)
  await Promise.all([
    fs.writeFile(stdoutFile, ''),
    fs.writeFile(stderrFile, ''),
  ])
  const {chromium} = await import('playwright-core')
  const context = await chromium.launchPersistentContext(profilePath, {
    args: [
      `--profile-directory=${profileDirectory}`,
      '--no-first-run',
      '--no-default-browser-check',
      '--disable-default-apps',
      '--disable-component-update',
      '--test-type',
      '--window-size=1280,800',
      '--use-mock-keychain',
      '--enable-unsafe-extension-debugging',
      '--enable-logging',
      ...extraArgs,
    ],
    env: {...process.env, ...env, CHROME_LOG_FILE: stderrFile},
    executablePath,
    headless: false,
    ignoreDefaultArgs: [
      '--disable-component-extensions-with-background-pages',
      '--disable-extensions',
    ],
    viewport: null,
  })
  const browser = context.browser()
  if (!browser) throw new Error('Extension CDP session has no browser')
  const browserSession = await browser.newBrowserCDPSession()
  const events = {
    browserExits: [],
    console: [],
    crashes: [],
    disconnected: [],
    pageErrors: [],
  }
  let closing = false
  browser.on('disconnected', () => {
    if (!closing) events.disconnected.push({time: new Date().toISOString()})
  })
  for (const page of context.pages()) {
    await page.emulateMedia({colorScheme: null})
    attachPageMonitor(page, events)
  }
  context.on('page', page => attachPageMonitor(page, events))
  const version = await browserSession.send('Browser.getVersion')
  const processes = processesForProfile(await listProcesses(), profilePath)
  const mainProcess = processes.find(item =>
    !item.command.includes('Brave Browser Development Helper'))

  return {
    browser,
    browserSession,
    context,
    events,
    process: {child: {pid: mainProcess?.pid || null}},
    profileDirectory,
    profilePath,
    version,
    async close() {
      closing = true
      const disconnected = new Promise(resolve =>
        browser.once('disconnected', resolve))
      await browserSession.send('Browser.close').catch(() => {})
      await Promise.race([
        disconnected,
        new Promise(resolve => setTimeout(resolve, 5000)),
      ])
      await stopProfileProcesses(profilePath).catch(() => {})
      await context.close().catch(() => {})
    },
    async processes() {
      return processesForProfile(await listProcesses(), profilePath)
    },
  }
}

async function visibleTextContrast(page) {
  const values = await page.evaluate(() => {
    function allElements(root, found = []) {
      for (const element of root.querySelectorAll?.('*') || []) {
        found.push(element)
        if (element.shadowRoot) allElements(element.shadowRoot, found)
      }
      return found
    }
    function parseColor(value) {
      const match = value.match(
        /^rgba?\(\s*([\d.]+)[, ]+\s*([\d.]+)[, ]+\s*([\d.]+)(?:\s*[,/]\s*([\d.]+))?\s*\)$/)
      if (!match) return null
      return {
        a: match[4] === undefined ? 1 : Number(match[4]),
        b: Number(match[3]),
        g: Number(match[2]),
        r: Number(match[1]),
      }
    }
    function composite(top, bottom) {
      const alpha = top.a + bottom.a * (1 - top.a)
      if (alpha === 0) return {a: 0, b: 0, g: 0, r: 0}
      return {
        a: alpha,
        b: (top.b * top.a + bottom.b * bottom.a * (1 - top.a)) / alpha,
        g: (top.g * top.a + bottom.g * bottom.a * (1 - top.a)) / alpha,
        r: (top.r * top.a + bottom.r * bottom.a * (1 - top.a)) / alpha,
      }
    }
    function background(element) {
      let accumulated = {a: 0, b: 0, g: 0, r: 0}
      let current = element
      while (current) {
        const layer = parseColor(getComputedStyle(current).backgroundColor)
        if (layer) accumulated = composite(accumulated, layer)
        if (accumulated.a >= 0.999) break
        current = current.parentElement || current.getRootNode()?.host || null
      }
      if (accumulated.a < 0.999) {
        accumulated = composite(accumulated, {a: 1, b: 255, g: 255, r: 255})
      }
      return `rgb(${Math.round(accumulated.r)}, ${Math.round(accumulated.g)}, ${Math.round(accumulated.b)})`
    }
    function inactive(element) {
      let current = element
      while (current) {
        if (current.matches?.(':disabled') || current.hasAttribute?.('disabled') ||
            current.hasAttribute?.('inert') ||
            current.getAttribute?.('aria-disabled') === 'true') {
          return true
        }
        current = current.parentElement || current.getRootNode()?.host || null
      }
      return false
    }
    return allElements(document.body).flatMap(element => {
      const style = getComputedStyle(element)
      const rect = element.getBoundingClientRect()
      const text = [...element.childNodes]
        .filter(node => node.nodeType === Node.TEXT_NODE)
        .map(node => node.textContent.trim())
        .filter(Boolean)
        .join(' ')
      if (!text || inactive(element) || style.visibility === 'hidden' || style.display === 'none' ||
          rect.width === 0 || rect.height === 0) {
        return []
      }
      return [{
        background: background(element),
        color: style.color,
        fontSize: style.fontSize,
        selector: element.id ? `#${element.id}` : element.tagName.toLowerCase(),
        text: text.slice(0, 100),
      }]
    })
  })
  return values.map(value => ({
    ...value,
    ratio: contrastRatio(value.color, value.background),
  })).filter(value => value.ratio !== null && value.ratio < 4.5)
}

async function visibleComponentContrast(page) {
  const values = await page.evaluate(() => {
    function allElements(root, found = []) {
      for (const element of root.querySelectorAll?.('*') || []) {
        found.push(element)
        if (element.shadowRoot) allElements(element.shadowRoot, found)
      }
      return found
    }
    function parseColor(value) {
      const match = value.match(
        /^rgba?\(\s*([\d.]+)[, ]+\s*([\d.]+)[, ]+\s*([\d.]+)(?:\s*[,/]\s*([\d.]+))?\s*\)$/)
      if (!match) return null
      return {
        a: match[4] === undefined ? 1 : Number(match[4]),
        b: Number(match[3]),
        g: Number(match[2]),
        r: Number(match[1]),
      }
    }
    function composite(top, bottom) {
      const alpha = top.a + bottom.a * (1 - top.a)
      if (alpha === 0) return {a: 0, b: 0, g: 0, r: 0}
      return {
        a: alpha,
        b: (top.b * top.a + bottom.b * bottom.a * (1 - top.a)) / alpha,
        g: (top.g * top.a + bottom.g * bottom.a * (1 - top.a)) / alpha,
        r: (top.r * top.a + bottom.r * bottom.a * (1 - top.a)) / alpha,
      }
    }
    function background(element) {
      let accumulated = {a: 0, b: 0, g: 0, r: 0}
      let current = element.parentElement || element.getRootNode()?.host || null
      while (current) {
        const layer = parseColor(getComputedStyle(current).backgroundColor)
        if (layer) accumulated = composite(accumulated, layer)
        if (accumulated.a >= 0.999) break
        current = current.parentElement || current.getRootNode()?.host || null
      }
      if (accumulated.a < 0.999) {
        accumulated = composite(accumulated, {a: 1, b: 255, g: 255, r: 255})
      }
      return `rgb(${Math.round(accumulated.r)}, ${Math.round(accumulated.g)}, ${Math.round(accumulated.b)})`
    }
    function visible(element) {
      const style = getComputedStyle(element)
      const rect = element.getBoundingClientRect()
      return style.display !== 'none' && style.visibility !== 'hidden' &&
        Number(style.opacity) > 0 && rect.width > 0 && rect.height > 0
    }
    function inactive(element) {
      let current = element
      while (current) {
        if (current.matches?.(':disabled') || current.hasAttribute?.('disabled') ||
            current.hasAttribute?.('inert') ||
            current.getAttribute?.('aria-disabled') === 'true') {
          return true
        }
        current = current.parentElement || current.getRootNode()?.host || null
      }
      return false
    }
    return allElements(document.body).flatMap(element => {
      if (!visible(element) || inactive(element)) return []
      const style = getComputedStyle(element)
      const selector = element.id
        ? `${element.tagName.toLowerCase()}#${element.id}`
        : element.tagName.toLowerCase()
      if (element.matches('path, svg, cr-icon, iron-icon')) {
        const paint = style.stroke && style.stroke !== 'none'
          ? style.stroke
          : style.fill && style.fill !== 'none' ? style.fill : style.color
        return [{background: background(element), color: paint, selector, type: 'icon'}]
      }
      if (element.matches('button, cr-button, input, select, textarea')) {
        const borderWidth = Number.parseFloat(style.borderTopWidth)
        if (borderWidth > 0 && style.borderTopStyle !== 'none') {
          return [{
            background: background(element),
            color: style.borderTopColor,
            selector,
            type: 'control-border',
          }]
        }
      }
      return []
    })
  })
  return values.map(value => ({
    ...value,
    ratio: contrastRatio(value.color, value.background),
  })).filter(value => value.ratio !== null && value.ratio < 3)
}

async function layoutFailures(page) {
  return await page.evaluate(() => {
    function allElements(root, found = []) {
      for (const element of root.querySelectorAll?.('*') || []) {
        found.push(element)
        if (element.shadowRoot) allElements(element.shadowRoot, found)
      }
      return found
    }
    function selector(element) {
      const tag = element.tagName.toLowerCase()
      return element.id ? `${tag}#${element.id}` : tag
    }
    function visible(element) {
      const style = getComputedStyle(element)
      const rect = element.getBoundingClientRect()
      return style.display !== 'none' && style.visibility !== 'hidden' &&
        Number(style.opacity) > 0 && rect.width > 0 && rect.height > 0
    }
    function composedContains(ancestor, node) {
      let current = node
      while (current) {
        if (current === ancestor) return true
        current = current.parentElement || current.getRootNode?.().host || null
      }
      return false
    }
    const issues = []
    if (document.documentElement.scrollWidth > window.innerWidth + 2) {
      issues.push({
        actual: document.documentElement.scrollWidth,
        expected: window.innerWidth,
        kind: 'horizontal-page-overflow',
        selector: 'html',
      })
    }
    const controls = []
    for (const element of allElements(document)) {
      if (!visible(element)) continue
      const style = getComputedStyle(element)
      const rect = element.getBoundingClientRect()
      const isControl = element.matches([
        'button', 'cr-button', 'cr-icon-button', 'cr-toggle', 'input',
        'select', 'textarea', '[role="button"]', '[role="menuitem"]',
        '[role="tab"]',
      ].join(','))
      if (isControl) {
        controls.push({element, rect})
        const clippedWidth = element.scrollWidth > element.clientWidth + 2
        const clippedHeight = element.scrollHeight > element.clientHeight + 2
        if ((clippedWidth || clippedHeight) && style.overflow !== 'visible') {
          issues.push({
            actual: `${element.scrollWidth}x${element.scrollHeight}`,
            expected: `${element.clientWidth}x${element.clientHeight}`,
            kind: 'control-content-clipped',
            selector: selector(element),
          })
        }
      }
    }
    for (let leftIndex = 0; leftIndex < controls.length; leftIndex += 1) {
      const left = controls[leftIndex]
      for (let rightIndex = leftIndex + 1; rightIndex < controls.length; rightIndex += 1) {
        const right = controls[rightIndex]
        if (composedContains(left.element, right.element) ||
            composedContains(right.element, left.element)) {
          continue
        }
        const overlapWidth = Math.min(left.rect.right, right.rect.right) -
          Math.max(left.rect.left, right.rect.left)
        const overlapHeight = Math.min(left.rect.bottom, right.rect.bottom) -
          Math.max(left.rect.top, right.rect.top)
        if (overlapWidth > 2 && overlapHeight > 2) {
          issues.push({
            actual: `${Math.round(overlapWidth)}x${Math.round(overlapHeight)}`,
            kind: 'interactive-controls-overlap',
            selector: `${selector(left.element)} <> ${selector(right.element)}`,
          })
        }
      }
    }
    return issues.slice(0, 100)
  })
}

export async function auditCurrentPage(page) {
  return {
    componentContrastFailures: await visibleComponentContrast(page),
    contrastFailures: await visibleTextContrast(page),
    layoutFailures: await layoutFailures(page),
  }
}

export async function navigateAndCapture({
  allowHttpErrors = false,
  colorScheme = null,
  page,
  screenshot,
  url,
  validateContrast = false,
  validateLayout = false,
  waitAfterMs = 1200,
}) {
  const startedAt = Date.now()
  await page.emulateMedia({colorScheme})
  const response = await page.goto(url, {timeout: 45000, waitUntil: 'domcontentloaded'})
  await page.waitForTimeout(waitAfterMs)
  const state = await page.evaluate(() => ({
    bodyText: (() => {
      function collect(root, values = []) {
        if (root instanceof HTMLElement && root.innerText?.trim()) {
          values.push(root.innerText.trim())
        }
        for (const element of root.querySelectorAll?.('*') || []) {
          if (element.shadowRoot) collect(element.shadowRoot, values)
        }
        return values
      }
      return collect(document.body).join('\n').slice(0, 4000)
    })(),
    documentState: document.readyState,
    height: document.documentElement.scrollHeight,
    elementCount: document.querySelectorAll('*').length,
    title: document.title,
    width: document.documentElement.scrollWidth,
  }))
  const status = response?.status() ?? null
  if (!allowHttpErrors && status !== null && status >= 400) {
    throw new Error(`${url} returned HTTP ${status}`)
  }
  const loadedInternalPage = url.startsWith('brave://') && state.elementCount > 2
  if (!state.bodyText.trim() && !loadedInternalPage) {
    throw new Error(`${url} rendered an empty body`)
  }
  if (/Aw, Snap!|Error code:\s*\d+/i.test(state.bodyText)) {
    throw new Error(`${url} rendered a crash page`)
  }
  await fs.mkdir(path.dirname(screenshot), {recursive: true})
  await page.screenshot({path: screenshot, fullPage: false})
  const audit = validateContrast || validateLayout
    ? await auditCurrentPage(page)
    : {componentContrastFailures: [], contrastFailures: [], layoutFailures: []}
  const contrastFailures = validateContrast ? audit.contrastFailures : []
  const componentContrastFailures = validateContrast
    ? audit.componentContrastFailures
    : []
  const geometryFailures = validateLayout ? audit.layoutFailures : []
  return {
    componentContrastFailures,
    contrastFailures,
    documentState: state.documentState,
    durationMs: Date.now() - startedAt,
    finalUrl: page.url(),
    layoutFailures: geometryFailures,
    screenshot,
    size: {height: state.height, width: state.width},
    status,
    title: state.title,
    url,
  }
}
