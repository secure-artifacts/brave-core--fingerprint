import {createHash} from 'node:crypto'
import {createReadStream} from 'node:fs'
import fs from 'node:fs/promises'
import net from 'node:net'
import os from 'node:os'
import path from 'node:path'
import {spawn} from 'node:child_process'

export async function pathExists(target) {
  try {
    await fs.access(target)
    return true
  } catch {
    return false
  }
}

export async function run(command, args = [], options = {}) {
  const startedAt = Date.now()
  return await new Promise((resolve, reject) => {
    const child = spawn(command, args, {
      cwd: options.cwd,
      env: {...process.env, ...options.env},
      stdio: ['ignore', 'pipe', 'pipe'],
    })
    const stdout = []
    const stderr = []
    let timedOut = false
    let forceTimer = null
    const timeoutTimer = options.timeoutMs ? setTimeout(() => {
      timedOut = true
      child.kill('SIGTERM')
      forceTimer = setTimeout(() => child.kill('SIGKILL'), 5000)
    }, options.timeoutMs) : null
    child.stdout.on('data', chunk => stdout.push(chunk))
    child.stderr.on('data', chunk => stderr.push(chunk))
    child.on('error', reject)
    child.on('close', (code, signal) => {
      if (timeoutTimer) clearTimeout(timeoutTimer)
      if (forceTimer) clearTimeout(forceTimer)
      const result = {
        code,
        durationMs: Date.now() - startedAt,
        signal,
        stderr: Buffer.concat(stderr).toString('utf8'),
        stdout: Buffer.concat(stdout).toString('utf8'),
        timedOut,
      }
      if (options.check && (code !== 0 || timedOut)) {
        const error = new Error(
          timedOut
            ? `${command} timed out after ${options.timeoutMs}ms`
            : `${command} exited ${code}: ${result.stderr || result.stdout}`)
        error.result = result
        reject(error)
        return
      }
      resolve(result)
    })
  })
}

export async function sha256(file) {
  return await new Promise((resolve, reject) => {
    const hash = createHash('sha256')
    const input = createReadStream(file)
    input.on('error', reject)
    input.on('data', chunk => hash.update(chunk))
    input.on('end', () => resolve(hash.digest('hex')))
  })
}

export async function pngDimensions(file) {
  const contents = await fs.readFile(file)
  const signature = contents.subarray(0, 8).toString('hex')
  if (signature !== '89504e470d0a1a0a' || contents.length < 24) {
    throw new Error(`Not a valid PNG: ${file}`)
  }
  return {
    height: contents.readUInt32BE(20),
    width: contents.readUInt32BE(16),
  }
}

export async function mapLimit(items, limit, operation) {
  const results = new Array(items.length)
  let nextIndex = 0
  async function worker() {
    while (nextIndex < items.length) {
      const index = nextIndex
      nextIndex += 1
      results[index] = await operation(items[index], index)
    }
  }
  await Promise.all(Array.from({length: Math.min(limit, items.length)}, worker))
  return results
}

export async function findFreePort() {
  return await new Promise((resolve, reject) => {
    const server = net.createServer()
    server.unref()
    server.on('error', reject)
    server.listen(0, '127.0.0.1', () => {
      const address = server.address()
      server.close(() => resolve(address.port))
    })
  })
}

export async function waitForJson(url, timeoutMs = 30000) {
  const deadline = Date.now() + timeoutMs
  let lastError
  while (Date.now() < deadline) {
    try {
      const response = await fetch(url)
      if (response.ok) {
        return await response.json()
      }
      lastError = new Error(`${response.status} ${response.statusText}`)
    } catch (error) {
      lastError = error
    }
    await new Promise(resolve => setTimeout(resolve, 250))
  }
  throw new Error(`Timed out waiting for ${url}: ${lastError}`)
}

export async function listProcesses() {
  const result = await run('ps', ['-axo', 'pid=,ppid=,state=,rss=,command='], {
    check: true,
  })
  return result.stdout.split('\n').flatMap(line => {
    const match = line.match(/^\s*(\d+)\s+(\d+)\s+(\S+)\s+(\d+)\s+(.+)$/)
    if (!match) {
      return []
    }
    return [{
      command: match[5],
      pid: Number(match[1]),
      ppid: Number(match[2]),
      rssKb: Number(match[4]),
      state: match[3],
    }]
  })
}

export function processesForProfile(processes, profilePath) {
  const roots = processes.filter(process =>
    process.command.includes('Brave Browser Development') &&
    (() => {
      const needle = `--user-data-dir=${profilePath}`
      const index = process.command.indexOf(needle)
      const next = process.command[index + needle.length]
      return index >= 0 && (next === undefined || /\s/.test(next))
    })())
  const selected = new Set(roots.map(process => process.pid))
  let changed = true
  while (changed) {
    changed = false
    for (const item of processes) {
      if (!selected.has(item.pid) && selected.has(item.ppid)) {
        selected.add(item.pid)
        changed = true
      }
    }
  }
  return processes.filter(process => selected.has(process.pid))
}

export async function stopProfileProcesses(profilePath, timeoutMs = 5000) {
  const first = processesForProfile(await listProcesses(), profilePath)
  const tracked = new Set(first.map(process => process.pid))
  for (const item of first) {
    try {
      process.kill(item.pid, 'SIGTERM')
    } catch (error) {
      if (error.code !== 'ESRCH') {
        throw error
      }
    }
  }

  const deadline = Date.now() + timeoutMs
  let remaining = first
  while (remaining.length > 0 && Date.now() < deadline) {
    await new Promise(resolve => setTimeout(resolve, 200))
    const processes = await listProcesses()
    const discovered = processesForProfile(processes, profilePath)
    for (const item of discovered) tracked.add(item.pid)
    remaining = processes.filter(item => tracked.has(item.pid))
  }
  for (const item of remaining) {
    try {
      process.kill(item.pid, 'SIGKILL')
    } catch (error) {
      if (error.code !== 'ESRCH') {
        throw error
      }
    }
  }
  if (remaining.length > 0) {
    await new Promise(resolve => setTimeout(resolve, 1000))
  }
  const processes = await listProcesses()
  const residual = processes.filter(item => tracked.has(item.pid))
  return {
    forced: remaining.map(process => process.pid),
    residual,
    stopped: first.map(process => process.pid),
  }
}

export async function stopAllQaProcesses() {
  const profiles = new Set()
  for (const item of await listProcesses()) {
    if (!item.command.includes('Brave Browser Development')) {
      continue
    }
    const match = item.command.match(/--user-data-dir=(?:"([^"]+)"|'([^']+)'|(\/tmp\/fingerprint-browser-[^\s]+))/)
    const profile = match?.[1] || match?.[2] || match?.[3]
    if (profile?.startsWith('/tmp/fingerprint-browser-')) {
      profiles.add(profile)
    }
  }
  const results = []
  for (const profile of profiles) {
    results.push({profile, ...await stopProfileProcesses(profile)})
  }
  return results
}

export async function snapshotCrashReports() {
  const directory = path.join(os.homedir(), 'Library', 'Logs', 'DiagnosticReports')
  if (!(await pathExists(directory))) {
    return []
  }
  const entries = await fs.readdir(directory, {withFileTypes: true})
  const reports = []
  for (const entry of entries) {
    if (!entry.isFile() || !entry.name.endsWith('.ips') ||
        !entry.name.startsWith('Brave Browser Development')) {
      continue
    }
    const file = path.join(directory, entry.name)
    const stat = await fs.stat(file)
    reports.push({file, mtimeMs: stat.mtimeMs, size: stat.size})
  }
  return reports.sort((left, right) => left.file.localeCompare(right.file))
}

export function newCrashReports(before, after) {
  const previous = new Map(before.map(report => [report.file, report]))
  return after.filter(report => {
    const old = previous.get(report.file)
    return !old || old.mtimeMs !== report.mtimeMs || old.size !== report.size
  })
}

export async function copyCrashReports(reports, destination) {
  await fs.mkdir(destination, {recursive: true})
  const copied = []
  for (const report of reports) {
    const target = path.join(destination, path.basename(report.file))
    await fs.copyFile(report.file, target)
    copied.push(target)
  }
  return copied
}

export async function scanFatalLogs(directory) {
  if (!(await pathExists(directory))) return []
  const failures = []
  for (const entry of await fs.readdir(directory, {withFileTypes: true})) {
    if (!entry.isFile() || !/-(?:stderr|stdout)\.log$/.test(entry.name)) continue
    const file = path.join(directory, entry.name)
    const contents = await fs.readFile(file, 'utf8')
    for (const [index, line] of contents.split('\n').entries()) {
      if (/CHECK failed|\bFATAL\b|\bDYLD\b|Aw, Snap!/i.test(line)) {
        failures.push({file, line: index + 1, text: line.slice(0, 1000)})
      }
    }
  }
  return failures
}

function requirePid(pid) {
  if (!Number.isInteger(pid) || pid <= 0) {
    throw new Error(`A valid QA browser PID is required, got ${pid}`)
  }
  return pid
}

export async function captureNativeScreenshot(target, pid) {
  requirePid(pid)
  await fs.mkdir(path.dirname(target), {recursive: true})
  await run('osascript', [
    '-e',
    `tell application "System Events" to set frontmost of first process whose unix id is ${pid} to true`,
  ])
  await new Promise(resolve => setTimeout(resolve, 300))
  const program = `
import AppKit
import Darwin
import ScreenCaptureKit

let expectedPid = pid_t(${pid})
let target = ${JSON.stringify(target)}
_ = NSApplication.shared

Task {
  do {
    let content = try await SCShareableContent.excludingDesktopWindows(
      false, onScreenWindowsOnly: false)
    let candidates = content.windows.filter {
      $0.owningApplication?.processID == expectedPid &&
        $0.windowLayer == 0 && $0.frame.width >= 640 && $0.frame.height >= 480
    }
    guard let window = candidates.max(by: {
      $0.frame.width * $0.frame.height < $1.frame.width * $1.frame.height
    }) else {
      fputs("Could not identify the main Brave QA window\\n", stderr)
      exit(2)
    }
    let display = content.displays.max(by: { left, right in
      left.frame.intersection(window.frame).width *
          left.frame.intersection(window.frame).height <
        right.frame.intersection(window.frame).width *
          right.frame.intersection(window.frame).height
    })
    let scale = display.map { Double($0.width) / $0.frame.width } ??
      (NSScreen.main?.backingScaleFactor ?? 1)
    let configuration = SCStreamConfiguration()
    configuration.width = max(1, Int((window.frame.width * scale).rounded()))
    configuration.height = max(1, Int((window.frame.height * scale).rounded()))
    configuration.showsCursor = false
    let image = try await SCScreenshotManager.captureImage(
      contentFilter: SCContentFilter(desktopIndependentWindow: window),
      configuration: configuration)
    let representation = NSBitmapImageRep(cgImage: image)
    guard let data = representation.representation(using: .png, properties: [:]) else {
      fputs("Could not encode native screenshot\\n", stderr)
      exit(3)
    }
    try data.write(to: URL(fileURLWithPath: target))
    exit(0)
  } catch {
    fputs("\\(error)\\n", stderr)
    exit(4)
  }
}
RunLoop.main.run()
`
  let result
  for (let attempt = 1; attempt <= 3; attempt += 1) {
    result = await run('swift', ['-e', program], {timeoutMs: 30000})
    if (result.code === 0) return target
    if (attempt < 3) {
      await new Promise(resolve => setTimeout(resolve, attempt * 500))
    }
  }
  throw new Error(`ScreenCaptureKit failed: ${result.stderr.trim()}`)
}

export async function clickNativeText(screenshot, expectedText, pid) {
  requirePid(pid)
  if (!(await pathExists(screenshot))) {
    throw new Error(`Native screenshot does not exist: ${screenshot}`)
  }
  const program = `
import AppKit
import CoreGraphics
import Foundation
import Vision

let imagePath = ${JSON.stringify(screenshot)}
let expectedText = ${JSON.stringify(expectedText)}
let expectedPid = ${pid}

func normalized(_ value: String) -> String {
  value.lowercased().split(whereSeparator: { $0.isWhitespace })
    .joined(separator: " ")
}

func attribute(_ element: AXUIElement, _ name: CFString) -> AnyObject? {
  var value: CFTypeRef?
  guard AXUIElementCopyAttributeValue(element, name, &value) == .success else {
    return nil
  }
  return value
}

func findButton(_ element: AXUIElement, _ expected: String) -> AXUIElement? {
  let role = attribute(element, kAXRoleAttribute as CFString) as? String ?? ""
  let title = attribute(element, kAXTitleAttribute as CFString) as? String ?? ""
  let description =
    attribute(element, kAXDescriptionAttribute as CFString) as? String ?? ""
  if role == kAXButtonRole as String &&
      (normalized(title) == expected || normalized(description) == expected) {
    return element
  }
  let children =
    attribute(element, kAXChildrenAttribute as CFString) as? [AXUIElement] ?? []
  for child in children {
    if let button = findButton(child, expected) { return button }
  }
  return nil
}

let normalizedExpected = normalized(expectedText)
let application = AXUIElementCreateApplication(pid_t(expectedPid))
if let button = findButton(application, normalizedExpected),
    AXUIElementPerformAction(button, kAXPressAction as CFString) == .success {
  print("AXPress")
  exit(0)
}

guard let image = NSImage(contentsOfFile: imagePath),
      let cgImage = image.cgImage(forProposedRect: nil, context: nil, hints: nil) else {
  fputs("Could not load native screenshot\\n", stderr)
  exit(2)
}

let request = VNRecognizeTextRequest()
request.recognitionLevel = .accurate
request.usesLanguageCorrection = false
try VNImageRequestHandler(cgImage: cgImage, options: [:]).perform([request])

guard let observation = (request.results ?? []).first(where: { observation in
  guard let text = observation.topCandidates(1).first?.string else { return false }
  return normalized(text) == normalizedExpected
}) else {
  fputs("Vision did not find text: \\(expectedText)\\n", stderr)
  exit(3)
}

let windows = CGWindowListCopyWindowInfo(
  [.optionOnScreenOnly, .excludeDesktopElements], kCGNullWindowID) as! [[String: Any]]
let candidates = windows.filter { window in
  guard (window[kCGWindowOwnerPID as String] as? Int) == expectedPid,
        (window[kCGWindowLayer as String] as? Int) == 0,
        let bounds = CGRect(dictionaryRepresentation:
          window[kCGWindowBounds as String] as! CFDictionary) else {
    return false
  }
  return bounds.width >= 640 && bounds.height >= 480
}
guard let window = candidates.max(by: { left, right in
        let leftBounds = CGRect(dictionaryRepresentation:
          left[kCGWindowBounds as String] as! CFDictionary) ?? .zero
        let rightBounds = CGRect(dictionaryRepresentation:
          right[kCGWindowBounds as String] as! CFDictionary) ?? .zero
        return leftBounds.width * leftBounds.height <
          rightBounds.width * rightBounds.height
      }),
      let bounds = CGRect(dictionaryRepresentation:
        window[kCGWindowBounds as String] as! CFDictionary) else {
  fputs("Could not find QA browser window\\n", stderr)
  exit(4)
}

let box = observation.boundingBox
let point = CGPoint(
  x: bounds.minX + box.midX * bounds.width,
  y: bounds.minY + (1 - box.midY) * bounds.height)
guard CGPreflightPostEventAccess() else {
  fputs("macOS post-event access is not granted\\n", stderr)
  exit(5)
}
let source = CGEventSource(stateID: .hidSystemState)
CGEvent(mouseEventSource: source, mouseType: .mouseMoved,
        mouseCursorPosition: point, mouseButton: .left)?.post(tap: .cghidEventTap)
usleep(100_000)
CGEvent(mouseEventSource: source, mouseType: .leftMouseDown,
        mouseCursorPosition: point, mouseButton: .left)?.post(tap: .cghidEventTap)
CGEvent(mouseEventSource: source, mouseType: .leftMouseUp,
        mouseCursorPosition: point, mouseButton: .left)?.post(tap: .cghidEventTap)
print("\\(point.x) \\(point.y)")
`
  const result = await run('swift', ['-e', program])
  if (result.code !== 0) {
    throw new Error(`Could not click native text ${expectedText}: ${result.stderr.trim()}`)
  }
  return result.stdout.trim()
}

export async function clickNativeWindowOffset(xFromRight, yFromTop, pid) {
  requirePid(pid)
  if (!Number.isFinite(xFromRight) || xFromRight < 0 ||
      !Number.isFinite(yFromTop) || yFromTop < 0) {
    throw new Error(
      `Valid QA window offsets are required, got ${xFromRight}, ${yFromTop}`)
  }
  const program = `
import AppKit
import CoreGraphics
import Foundation

let expectedPid = ${pid}
let xFromRight = CGFloat(${xFromRight})
let yFromTop = CGFloat(${yFromTop})
let windows = CGWindowListCopyWindowInfo(
  [.optionOnScreenOnly, .excludeDesktopElements], kCGNullWindowID) as! [[String: Any]]
let candidates = windows.filter { window in
  guard (window[kCGWindowOwnerPID as String] as? Int) == expectedPid,
        (window[kCGWindowLayer as String] as? Int) == 0,
        let bounds = CGRect(dictionaryRepresentation:
          window[kCGWindowBounds as String] as! CFDictionary) else {
    return false
  }
  return bounds.width >= 640 && bounds.height >= 480
}
guard let window = candidates.max(by: { left, right in
        let leftBounds = CGRect(dictionaryRepresentation:
          left[kCGWindowBounds as String] as! CFDictionary) ?? .zero
        let rightBounds = CGRect(dictionaryRepresentation:
          right[kCGWindowBounds as String] as! CFDictionary) ?? .zero
        return leftBounds.width * leftBounds.height <
          rightBounds.width * rightBounds.height
      }),
      let bounds = CGRect(dictionaryRepresentation:
        window[kCGWindowBounds as String] as! CFDictionary) else {
  fputs("Could not find QA browser window\\n", stderr)
  exit(2)
}
guard xFromRight <= bounds.width, yFromTop <= bounds.height else {
  fputs("QA window offset is outside the browser window\\n", stderr)
  exit(3)
}
guard CGPreflightPostEventAccess() else {
  fputs("macOS post-event access is not granted\\n", stderr)
  exit(4)
}
let point = CGPoint(x: bounds.maxX - xFromRight, y: bounds.minY + yFromTop)
let source = CGEventSource(stateID: .hidSystemState)
CGEvent(mouseEventSource: source, mouseType: .mouseMoved,
        mouseCursorPosition: point, mouseButton: .left)?.post(tap: .cghidEventTap)
usleep(100_000)
CGEvent(mouseEventSource: source, mouseType: .leftMouseDown,
        mouseCursorPosition: point, mouseButton: .left)?.post(tap: .cghidEventTap)
CGEvent(mouseEventSource: source, mouseType: .leftMouseUp,
        mouseCursorPosition: point, mouseButton: .left)?.post(tap: .cghidEventTap)
print("\\(point.x) \\(point.y)")
`
  const result = await run('swift', ['-e', program])
  if (result.code !== 0) {
    throw new Error(`Could not click QA window offset: ${result.stderr.trim()}`)
  }
  return result.stdout.trim()
}

export async function nativeScreenshotHasText(screenshot, expectedText) {
  if (!(await pathExists(screenshot))) return false
  const program = `
import AppKit
import Foundation
import Vision

guard let image = NSImage(contentsOfFile: ${JSON.stringify(screenshot)}),
      let cgImage = image.cgImage(forProposedRect: nil, context: nil, hints: nil) else {
  exit(2)
}
let expected = ${JSON.stringify(expectedText)}.lowercased()
  .split(whereSeparator: { $0.isWhitespace }).joined(separator: " ")
let request = VNRecognizeTextRequest()
request.recognitionLevel = .accurate
request.usesLanguageCorrection = false
try VNImageRequestHandler(cgImage: cgImage, options: [:]).perform([request])
let found = (request.results ?? []).contains { observation in
  guard let text = observation.topCandidates(1).first?.string else { return false }
  return text.lowercased().split(whereSeparator: { $0.isWhitespace })
    .joined(separator: " ") == expected
}
print(found ? "true" : "false")
`
  const result = await run('swift', ['-e', program], {check: true})
  return result.stdout.trim() === 'true'
}

export async function nativeShortcut(key, modifiers = [], pid) {
  requirePid(pid)
  const modifierText = modifiers.length > 0
    ? ` using {${modifiers.map(modifier => `${modifier} down`).join(', ')}}`
    : ''
  return await run('osascript', [
    '-e',
    `tell application "System Events" to set frontmost of first process whose unix id is ${pid} to true`,
    '-e',
    'delay 0.2',
    '-e',
    `tell application "System Events" to keystroke "${key}"${modifierText}`,
  ], {check: true})
}

export async function nativeKeyCode(keyCode, pid) {
  requirePid(pid)
  if (!Number.isInteger(keyCode) || keyCode < 0) {
    throw new Error(`A valid macOS key code is required, got ${keyCode}`)
  }
  return await run('osascript', [
    '-e',
    `tell application "System Events" to set frontmost of first process whose unix id is ${pid} to true`,
    '-e',
    'delay 0.2',
    '-e',
    `tell application "System Events" to key code ${keyCode}`,
  ], {check: true})
}

export async function setFrontWindowSize(width, height, pid) {
  requirePid(pid)
  return await run('osascript', [
    '-e',
    `tell application "System Events" to tell first process whose unix id is ${pid}`,
    '-e',
    'set frontmost to true',
    '-e',
    `set size of front window to {${Math.round(width)}, ${Math.round(height)}}`,
    '-e',
    'end tell',
  ], {check: true})
}

export async function setFrontWindowPosition(x, y, pid) {
  requirePid(pid)
  return await run('osascript', [
    '-e',
    `tell application "System Events" to tell first process whose unix id is ${pid}`,
    '-e',
    `set position of front window to {${Math.round(x)}, ${Math.round(y)}}`,
    '-e',
    'end tell',
  ], {check: true})
}

export function safeRunId(date = new Date()) {
  return date.toISOString().replace(/[:.]/g, '-').replace('T', '_').replace('Z', '')
}
