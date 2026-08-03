// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.
import { sendWithPromise } from 'chrome://resources/js/cr.js'
type ScreenPersona = {
  width: number
  height: number
  availWidth: number
  availHeight: number
  colorDepth: number
}

type WebGLPersona = {
  vendor: string
  renderer: string
}

type Persona = {
  personaId: string
  os: string
  profile: string
  userAgent: string
  platform: string
  uaPlatform: string
  hardwareConcurrency: number
  deviceMemory: number
  maxTouchPoints: number
  languages: string[]
  screen: ScreenPersona
  webgl: WebGLPersona
}

type WindowWithLoadTimeData = Window & {
  loadTimeData?: {
    getString(key: string): string
  }
}

type ActualFingerprint = {
  userAgent: string
  platform: string
  uaPlatform: string
  hardwareConcurrency: number
  deviceMemory: number | null
  maxTouchPoints: number
  languages: string[]
  screen: ScreenPersona
  webgl: WebGLPersona | null
  timezone: string
  uaData: Record<string, unknown> | null
}

type WebPageFingerprintResponse = {
  error?: string
  fingerprint?: ActualFingerprint
  url?: string
}

const summary = document.querySelector<HTMLDivElement>('#summary')!
const comparison =
  document.querySelector<HTMLTableSectionElement>('#comparison')!
const observed = document.querySelector<HTMLTableSectionElement>('#observed')!
const status = document.querySelector<HTMLDivElement>('#status')!
const refresh = document.querySelector<HTMLButtonElement>('#refresh')!

function valueText(value: unknown): string {
  if (value === null || value === undefined || value === '') {
    return '不可用'
  }
  if (typeof value === 'object') {
    return JSON.stringify(value)
  }
  return String(value)
}

function valuesEqual(expected: unknown, actual: unknown): boolean {
  if (Object.is(expected, actual)) {
    return true
  }
  if (Array.isArray(expected) && Array.isArray(actual)) {
    return (
      expected.length === actual.length
      && expected.every((value, index) => valuesEqual(value, actual[index]))
    )
  }
  if (
    expected
    && actual
    && typeof expected === 'object'
    && typeof actual === 'object'
  ) {
    const expectedRecord = expected as Record<string, unknown>
    const actualRecord = actual as Record<string, unknown>
    const expectedKeys = Object.keys(expectedRecord).sort()
    const actualKeys = Object.keys(actualRecord).sort()
    return (
      valuesEqual(expectedKeys, actualKeys)
      && expectedKeys.every((key) =>
        valuesEqual(expectedRecord[key], actualRecord[key]),
      )
    )
  }
  return false
}

async function readActualFingerprint(): Promise<WebPageFingerprintResponse> {
  const response = JSON.parse(
    await sendWithPromise<string>('getLastWebPageFingerprint'),
  ) as WebPageFingerprintResponse
  if (response.error || !response.fingerprint || !response.url) {
    throw new Error('fingerprint_unavailable')
  }
  return response
}

function readPersona(): Persona {
  const persona = (window as WindowWithLoadTimeData).loadTimeData?.getString(
    'fingerprintTestPersona',
  )
  if (!persona) {
    throw new Error('persona_unavailable')
  }
  return JSON.parse(persona) as Persona
}

function addSummary(label: string, value: unknown) {
  const item = document.createElement('div')
  item.className = 'summary-item'
  const labelElement = document.createElement('span')
  labelElement.className = 'summary-label'
  labelElement.textContent = label
  const valueElement = document.createElement('div')
  valueElement.className = 'summary-value'
  valueElement.textContent = valueText(value)
  item.append(labelElement, valueElement)
  summary.append(item)
}

function addComparisonRow(label: string, expected: unknown, actual: unknown) {
  const row = document.createElement('tr')
  const labelCell = document.createElement('td')
  labelCell.textContent = label
  const expectedCell = document.createElement('td')
  expectedCell.textContent = valueText(expected)
  const actualCell = document.createElement('td')
  actualCell.textContent = valueText(actual)
  const resultCell = document.createElement('td')
  resultCell.className = 'row-status'
  const isAvailable = actual !== null && actual !== undefined && actual !== ''
  const isMatch = isAvailable && valuesEqual(expected, actual)
  resultCell.dataset.state = isAvailable ? (isMatch ? 'pass' : 'fail') : 'na'
  resultCell.textContent = isAvailable ? (isMatch ? '匹配' : '不同') : '不可用'
  row.append(labelCell, expectedCell, actualCell, resultCell)
  comparison.append(row)
  return isMatch
}

function addObservedRow(label: string, value: unknown) {
  const row = document.createElement('tr')
  const labelCell = document.createElement('td')
  labelCell.textContent = label
  const valueCell = document.createElement('td')
  valueCell.colSpan = 3
  valueCell.textContent = valueText(value)
  row.append(labelCell, valueCell)
  observed.append(row)
}

async function load() {
  refresh.disabled = true
  status.dataset.state = 'warn'
  status.textContent = '正在加载检测数据...'
  summary.replaceChildren()
  comparison.replaceChildren()
  observed.replaceChildren()

  try {
    const response = await readActualFingerprint()
    const actual = response.fingerprint!
    const persona = readPersona()

    addSummary('用户配置文件', persona.profile)
    addSummary('浏览器身份 ID', persona.personaId)
    addSummary('浏览器身份系统', persona.os)
    addSummary('已检测网页', response.url)

    const matched = [
      addComparisonRow('User-Agent', persona.userAgent, actual.userAgent),
      addComparisonRow('Navigator 平台', persona.platform, actual.platform),
      addComparisonRow('UA-CH 平台', persona.uaPlatform, actual.uaPlatform),
      addComparisonRow(
        '硬件并发数',
        persona.hardwareConcurrency,
        actual.hardwareConcurrency,
      ),
      addComparisonRow(
        '设备内存（GB）',
        persona.deviceMemory,
        actual.deviceMemory,
      ),
      addComparisonRow(
        '最大触控点数',
        persona.maxTouchPoints,
        actual.maxTouchPoints,
      ),
      addComparisonRow('语言', persona.languages, actual.languages),
      addComparisonRow('屏幕', persona.screen, actual.screen),
      addComparisonRow('WebGL', persona.webgl, actual.webgl),
    ]

    const matchCount = matched.filter(Boolean).length
    status.dataset.state = matchCount === matched.length ? 'pass' : 'fail'
    status.textContent = `${matched.length} 项中有 ${matchCount} 项与当前浏览器身份匹配。`

    addObservedRow('网页时区', actual.timezone)
    addObservedRow('User-Agent Client Hints', actual.uaData)
    addObservedRow('WebGL 原始值', actual.webgl)
    addObservedRow('屏幕色深', actual.screen.colorDepth)
  } catch {
    status.dataset.state = 'fail'
    status.textContent =
      '无法读取指纹数据。请先在另一个标签页打开普通网站，然后重试。'
  } finally {
    refresh.disabled = false
  }
}

refresh.addEventListener('click', load)
window.addEventListener('load', () => {
  window.setTimeout(load, 500)
})
