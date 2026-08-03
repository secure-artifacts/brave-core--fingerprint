// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

import fs from 'node:fs/promises'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url))
const BRAVE_ROOT = path.resolve(SCRIPT_DIR, '../../..')

const ALLOWED_TECHNICAL_TERMS = [
  'Chrome Web Store',
  'User-Agent Client Hints',
  'Client Hints',
  'User-Agent',
  'Navigator',
  'Crashpad',
  'Cookie',
  'WebRTC',
  'WebGPU',
  'WebGL',
  'UA-CH',
  'Profile',
  'HTTPS',
  'HTTP',
  'IANA',
  'JSON',
  'ZIP',
  'ISO',
  'TCP',
  'UDP',
  'TLS',
  'DNS',
  'CDP',
  'GPU',
  'CPU',
  'API',
  'UA',
  'IP',
  'OS',
  'GB',
  'MB',
]

export const DEFAULT_SOURCE_SPECS = [
  {
    file: 'app/brave_generated_resources.grd',
    messageNamePattern:
      /^(?:IDS_DIAGNOSTICS_|IDS_FINGERPRINT_GUIDE_|IDS_EXPORT_DIAGNOSTICS$|IDS_OPEN_CRASH_FILE_LOCATION$|IDS_SHOW_FINGERPRINT_GUIDE$)/,
  },
  {
    file: 'app/brave_settings_strings.grdp',
    messageNamePattern: /FINGERPRINT_PROFILE_PROXY/,
  },
  { file: 'browser/fingerprint_browser/fingerprint_proxy_service.cc' },
  {
    file: 'browser/resources/settings/fingerprint_profile_proxy_page/fingerprint_profile_proxy_subpage.html',
  },
  {
    file: 'browser/resources/settings/fingerprint_profile_proxy_page/fingerprint_profile_proxy_subpage.ts',
  },
  { file: 'browser/fingerprint_browser/diagnostics/diagnostics_exporter.cc' },
  { file: 'browser/ui/views/toolbar/fingerprint_proxy_button.cc' },
  { file: 'browser/ui/webui/diagnostics/diagnostics_ui.cc' },
  { file: 'browser/ui/webui/fingerprint_test/fingerprint_test_ui.cc' },
  {
    file: 'browser/ui/webui/settings/fingerprint_profile_proxy_handler.cc',
  },
  { file: 'components/fingerprint_browser/browser/profile_proxy_config.cc' },
  { file: 'components/fingerprint_browser/resources/diagnostics.html' },
  { file: 'components/fingerprint_browser/resources/diagnostics.ts' },
  {
    file: 'components/fingerprint_browser/resources/fingerprint_guide.html',
    optional: true,
  },
  { file: 'components/fingerprint_browser/resources/fingerprint_test.html' },
  { file: 'components/fingerprint_browser/resources/fingerprint_test.ts' },
]

function replaceExceptNewlines(value) {
  return value.replace(/[^\n]/g, ' ')
}

function decodeEntities(value) {
  return value
    .replace(/&lt;/gi, '<')
    .replace(/&gt;/gi, '>')
    .replace(/&quot;/gi, '"')
    .replace(/&apos;/gi, "'")
    .replace(/&amp;/gi, '&')
    .replace(/&#(?:x([0-9a-f]+)|(\d+));/gi, (_, hex, decimal) =>
      String.fromCodePoint(Number.parseInt(hex || decimal, hex ? 16 : 10)),
    )
}

function normalizeText(value) {
  return decodeEntities(value)
    .replace(/\\(?:[0abfnrtv'"\\]|x[0-9a-f]{2}|u[0-9a-f]{4})/gi, ' ')
    .replace(/<ex>[\s\S]*?<\/ex>/gi, ' ')
    .replace(/<[^>]+>/g, ' ')
    .replace(/\$i18n\{[^}]+\}/g, ' ')
    .replace(/\[\[[\s\S]*?\]\]|\{\{[\s\S]*?\}\}/g, ' ')
    .replace(/\$\{[^}]*\}/g, ' ')
    .replace(/\s+/g, ' ')
    .trim()
}

export function disallowedEnglish(value) {
  let remainder = normalizeText(value)
  for (const term of ALLOWED_TECHNICAL_TERMS) {
    remainder = remainder.replace(
      new RegExp(
        `\\b${term.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}\\b(?:['’]s)?`,
        'gi',
      ),
      ' ',
    )
  }
  remainder = remainder
    .replace(/\b[a-z][a-z0-9+.-]*:\/\/[^\s<>"']+/gi, ' ')
    .replace(/\b(?:[a-z0-9-]+\.)+[a-z]{2,}\b/gi, ' ')
    .replace(
      /\b[A-Z][A-Za-z_+-]+\/[A-Z][A-Za-z_+-]+(?:\/[A-Z][A-Za-z_+-]+)?\b/g,
      ' ',
    )
    .replace(/\b(?:\d{1,3}\.){3}\d{1,3}\b/g, ' ')
    .replace(/\b[0-9a-f]{0,4}(?::[0-9a-f]{0,4}){2,}\b/gi, ' ')
    .replace(/(?:[A-Za-z]:\\|(?:\.\.?\/|\/))[^\s,;]+/g, ' ')
    .replace(
      /\b(?:localhost|[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12})\b/gi,
      ' ',
    )
    .replace(/%(?:\d+\$)?[-+#0 ]*\d*(?:\.\d+)?[a-z]|\$\d+/gi, ' ')
    .replace(/\b0x[0-9a-f]+\b|\b\d+(?:\.\d+)*(?:px|ms|s|%)?\b/gi, ' ')
    .replace(/\b[A-Za-z_$][A-Za-z0-9_$]*(?:[_.:][A-Za-z0-9_$-]+)+\b/g, ' ')
    .replace(
      /\b(?:[a-z]+[A-Z][A-Za-z0-9]*|[A-Z][a-z0-9]+(?:[A-Z][A-Za-z0-9]*)+)\b/g,
      ' ',
    )
    .replace(/\b[A-Za-z0-9]+(?:-[A-Za-z0-9]+)+\b/g, ' ')
    .replace(/\b[A-Z]{2,}(?:_[A-Z0-9]+)*\b/g, ' ')
    .replace(/\{[^}]*\}/g, ' ')
  return (remainder.match(/[A-Za-z]+(?:'[A-Za-z]+)?/g) || []).join(' ')
}

function lineAt(source, offset) {
  let line = 1
  for (let index = 0; index < offset; index += 1) {
    if (source[index] === '\n') line += 1
  }
  return line
}

function maskComments(source) {
  const output = [...source]
  let quote = null
  for (let index = 0; index < source.length; index += 1) {
    const character = source[index]
    if (quote) {
      if (character === '\\') {
        index += 1
      } else if (character === quote) {
        quote = null
      }
      continue
    }
    if (character === '"' || character === "'" || character === '`') {
      quote = character
      continue
    }
    if (character === '/' && source[index + 1] === '/') {
      let end = index
      while (end < source.length && source[end] !== '\n') end += 1
      for (let cursor = index; cursor < end; cursor += 1) output[cursor] = ' '
      index = end - 1
      continue
    }
    if (character === '/' && source[index + 1] === '*') {
      const end = source.indexOf('*/', index + 2)
      const stop = end < 0 ? source.length : end + 2
      for (let cursor = index; cursor < stop; cursor += 1) {
        if (output[cursor] !== '\n') output[cursor] = ' '
      }
      index = stop - 1
    }
  }
  return output.join('')
}

function readString(source, start) {
  if (source[start] === 'R' && source[start + 1] === '"') {
    const opening = source.indexOf('(', start + 2)
    if (opening < 0 || opening - start > 18) return null
    const delimiter = source.slice(start + 2, opening)
    const closing = source.indexOf(`)${delimiter}"`, opening + 1)
    if (closing < 0) return null
    return {
      end: closing + delimiter.length + 2,
      start,
      value: source.slice(opening + 1, closing),
    }
  }
  const quote = source[start]
  if (quote !== '"' && quote !== "'" && quote !== '`') return null
  for (let index = start + 1; index < source.length; index += 1) {
    if (source[index] === '\\') {
      index += 1
    } else if (source[index] === quote) {
      return { end: index + 1, start, value: source.slice(start + 1, index) }
    }
  }
  return null
}

function stringsInRange(source, masked, start, end) {
  const strings = []
  for (let index = start; index < end; index += 1) {
    if (masked[index] === 'R' && masked[index + 1] === '"') {
      const parsed = readString(source, index)
      if (parsed) {
        strings.push(parsed)
        index = parsed.end - 1
      }
    } else if ('"\'`'.includes(masked[index])) {
      const parsed = readString(source, index)
      if (parsed) {
        strings.push(parsed)
        index = parsed.end - 1
      }
    }
  }
  return strings
}

function expressionEnd(source, masked, start) {
  let depth = 0
  for (let index = start; index < source.length; index += 1) {
    if (masked[index] === 'R' && masked[index + 1] === '"') {
      const parsed = readString(source, index)
      if (parsed) index = parsed.end - 1
      continue
    }
    if ('"\'`'.includes(masked[index])) {
      const parsed = readString(source, index)
      if (parsed) index = parsed.end - 1
      continue
    }
    if ('([{'.includes(masked[index])) depth += 1
    if (')]}'.includes(masked[index])) depth -= 1
    if (depth === 0 && masked[index] === ';') return index
    if (depth === 0 && masked[index] === '\n') {
      const before = masked.slice(start, index).trimEnd().at(-1) || ''
      const after = masked.slice(index + 1).match(/^\s*(.)/)?.[1] || ''
      if (!'=?:,+-*/(&|'.includes(before) && !'?:.,)&|\'"`'.includes(after)) {
        return index
      }
    }
  }
  return source.length
}

function stringsInCallArgument(source, masked, open, wantedArgument) {
  const strings = []
  let argument = 0
  let depth = 0
  for (let index = open + 1; index < source.length; index += 1) {
    if (masked[index] === 'R' && masked[index + 1] === '"') {
      const parsed = readString(source, index)
      if (parsed) {
        if (argument === wantedArgument) strings.push(parsed)
        index = parsed.end - 1
      }
      continue
    }
    if ('"\'`'.includes(masked[index])) {
      const parsed = readString(source, index)
      if (parsed) {
        if (argument === wantedArgument) strings.push(parsed)
        index = parsed.end - 1
      }
      continue
    }
    if ('([{'.includes(masked[index])) {
      depth += 1
    } else if (')]}'.includes(masked[index])) {
      if (depth === 0) break
      depth -= 1
    } else if (masked[index] === ',' && depth === 0) {
      argument += 1
    }
  }
  return strings
}

function candidate(value, offset, kind) {
  const text = normalizeText(value)
  const english = disallowedEnglish(value)
  return english ? { english, kind, offset, text } : null
}

function isComparisonOperand(masked, string) {
  const before = masked.slice(Math.max(0, string.start - 16), string.start)
  const after = masked.slice(string.end, string.end + 16)
  return /(?:===?|!==?)\s*$/.test(before) || /^\s*(?:===?|!==?)/.test(after)
}

function extractHtml(source) {
  let masked = source.replace(/<!--[\s\S]*?-->/g, replaceExceptNewlines)
  masked = masked.replace(
    /<(?:style|script)\b[\s\S]*?<\/(?:style|script)\s*>/gi,
    replaceExceptNewlines,
  )
  const results = []
  const textPattern = />([^<]+)</g
  for (const match of masked.matchAll(textPattern)) {
    const item = candidate(match[1], match.index + 1, 'html-text')
    if (item) results.push(item)
  }
  const attributePattern =
    /\b(aria-label|alt|label|placeholder|title|value)\s*=\s*(["'])([\s\S]*?)\2/gi
  for (const match of masked.matchAll(attributePattern)) {
    if (
      match[1].toLowerCase() === 'value'
      && /^[a-z][a-z0-9_-]*$/.test(match[3])
    ) {
      continue
    }
    const offset = match.index + match[0].indexOf(match[3])
    const item = candidate(match[3], offset, `html-${match[1].toLowerCase()}`)
    if (item) results.push(item)
  }
  return results
}

function extractMessages(source, messageNamePattern) {
  const results = []
  const pattern = /<message\b([^>]*)>([\s\S]*?)<\/message>/gi
  for (const match of source.matchAll(pattern)) {
    const name = match[1].match(/\bname\s*=\s*["']([^"']+)["']/i)?.[1] || ''
    const included = !messageNamePattern || messageNamePattern.test(name)
    if (messageNamePattern) messageNamePattern.lastIndex = 0
    if (!included) continue
    const body = match[2].replace(/<ex>[\s\S]*?<\/ex>/gi, ' ')
    const offset = match.index + match[0].indexOf(match[2])
    const item = candidate(body, offset, `message:${name}`)
    if (item) results.push(item)
  }
  return results
}

function extractCode(source) {
  const masked = maskComments(source)
  const results = []
  const assignmentPattern =
    /(?:\.\s*(?:textContent|innerText|outerText|ariaLabel|label|placeholder|title|status_message|change_warning|error)|\b(?:status_message|change_warning)\b)\s*=(?!=)/g
  for (const match of masked.matchAll(assignmentPattern)) {
    const start = match.index + match[0].length
    for (const string of stringsInRange(
      source,
      masked,
      start,
      expressionEnd(source, masked, start),
    )) {
      if (isComparisonOperand(masked, string)) continue
      const item = candidate(string.value, string.start, 'visible-assignment')
      if (item) results.push(item)
    }
  }

  const callArguments = new Map([
    ['AddString', 1],
    ['FinishLookupFailure', 0],
    ['SetState', 1],
    ['addComparisonRow', 0],
    ['addObservedRow', 0],
    ['addSummary', 0],
  ])
  const callPattern =
    /\b(AddString|FinishLookupFailure|SetState|addComparisonRow|addObservedRow|addSummary)\s*\(/g
  for (const match of masked.matchAll(callPattern)) {
    const open = match.index + match[0].lastIndexOf('(')
    for (const string of stringsInCallArgument(
      source,
      masked,
      open,
      callArguments.get(match[1]),
    )) {
      const item = candidate(string.value, string.start, `call:${match[1]}`)
      if (item) results.push(item)
    }
  }

  const setAttributePattern = /\bsetAttribute\s*\(/g
  for (const match of masked.matchAll(setAttributePattern)) {
    const open = match.index + match[0].lastIndexOf('(')
    const name = stringsInCallArgument(source, masked, open, 0)[0]?.value
    if (!/^(?:aria-label|alt|label|placeholder|title)$/i.test(name || ''))
      continue
    for (const string of stringsInCallArgument(source, masked, open, 1)) {
      const item = candidate(string.value, string.start, 'call:setAttribute')
      if (item) results.push(item)
    }
  }

  for (const string of stringsInRange(source, masked, 0, source.length)) {
    const errorPattern = /["']error["']\s*:\s*["']([^"']+)["']/gi
    for (const match of string.value.matchAll(errorPattern)) {
      const item = candidate(match[1], string.start + match.index, 'json-error')
      if (item) results.push(item)
    }
  }
  return results
}

export function auditSource({ file, messageNamePattern, source }) {
  const extension = path.extname(file).toLowerCase()
  let candidates
  if (extension === '.html' || extension === '.htm') {
    candidates = extractHtml(source)
  } else if (['.grd', '.grdp', '.xml'].includes(extension)) {
    candidates = extractMessages(source, messageNamePattern)
  } else {
    candidates = extractCode(source)
  }
  const seen = new Set()
  return candidates.flatMap((item) => {
    const key = `${item.offset}:${item.text}`
    if (seen.has(key)) return []
    seen.add(key)
    return [{ ...item, file, line: lineAt(source, item.offset) }]
  })
}

export async function auditSources(specs, { root = process.cwd() } = {}) {
  const findings = []
  for (const spec of specs) {
    const file = path.resolve(root, spec.file)
    let source
    try {
      source = await fs.readFile(file, 'utf8')
    } catch (error) {
      if (spec.optional && error.code === 'ENOENT') continue
      throw error
    }
    findings.push(...auditSource({ ...spec, file, source }))
  }
  return findings
}

function usage() {
  return [
    'Usage: node audit_zh_visible_text.mjs [source-file ...]',
    '',
    'With no files, audits known fingerprint-browser UI sources.',
    'Exit 0: no findings. Exit 1: visible English found. Exit 2: input error.',
  ].join('\n')
}

export async function main(argv = process.argv.slice(2)) {
  if (argv.includes('--help') || argv.includes('-h')) {
    process.stdout.write(`${usage()}\n`)
    return 0
  }
  if (argv.some((argument) => argument.startsWith('-'))) {
    throw new Error(
      `Unknown option: ${argv.find((argument) => argument.startsWith('-'))}`,
    )
  }
  const specs =
    argv.length > 0
      ? argv.map((file) => ({ file: path.resolve(file) }))
      : DEFAULT_SOURCE_SPECS
  const findings = await auditSources(specs, {
    root: argv.length > 0 ? process.cwd() : BRAVE_ROOT,
  })
  for (const finding of findings) {
    const displayPath =
      path.relative(process.cwd(), finding.file) || finding.file
    process.stdout.write(
      `${displayPath}:${finding.line}: ${finding.text} [${finding.english}]\n`,
    )
  }
  process.stdout.write(
    findings.length === 0
      ? `PASS: ${specs.length} source files contain no disallowed visible English.\n`
      : `FAIL: found ${findings.length} visible English candidate(s).\n`,
  )
  return findings.length === 0 ? 0 : 1
}

if (
  process.argv[1]
  && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)
) {
  main()
    .then((code) => {
      process.exitCode = code
    })
    .catch((error) => {
      process.stderr.write(`${error.message}\n`)
      process.exitCode = 2
    })
}
