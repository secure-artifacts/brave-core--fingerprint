// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.
import { sendWithPromise } from 'chrome://resources/js/cr.js'

type LoadTimeData = {
  getString(id: string): string
  getStringF(id: string, ...args: Array<string | number>): string
}

interface WindowWithLoadTimeData extends Window {
  loadTimeData: LoadTimeData
}

const loadTimeData = (window as unknown as WindowWithLoadTimeData).loadTimeData

type ExportScope = 'latest_incident' | 'last_7_days'
type StatusState = 'progress' | 'success' | 'error' | 'cancelled'

type DiagnosticsState = {
  canExport: boolean
  guest: boolean
  exporting: boolean
  crashFolderAvailable: boolean
  lastExportAvailable: boolean
  localCrashCount: number
  defaultScope: ExportScope
}

type DiagnosticsExportResult = {
  success: boolean
  cancelled: boolean
  error: string
  crashCount: number
  archiveName: string
  omittedCrashCount: number
}

const exportCard = document.querySelector<HTMLElement>('#export-card')!
const scopeFieldset =
  document.querySelector<HTMLFieldSetElement>('#scope-fieldset')!
const scopeInputs = Array.from(
  document.querySelectorAll<HTMLInputElement>('input[name="scope"]'),
)
const availability = document.querySelector<HTMLElement>('#availability')!
const privacyConfirm =
  document.querySelector<HTMLInputElement>('#privacy-confirm')!
const exportButton = document.querySelector<HTMLButtonElement>('#export')!
const exportStatus = document.querySelector<HTMLElement>('#export-status')!
const statusIcon = document.querySelector<HTMLElement>('#status-icon')!
const statusTitle = document.querySelector<HTMLElement>('#status-title')!
const statusMessage = document.querySelector<HTMLElement>('#status-message')!
const outputPath = document.querySelector<HTMLElement>('#output-path')!
const openCrashFolder =
  document.querySelector<HTMLButtonElement>('#open-crash-folder')!
const openExportFolder = document.querySelector<HTMLButtonElement>(
  '#open-export-folder',
)!
const folderStatus = document.querySelector<HTMLElement>('#folder-status')!

let ready = false
let exporting = false

function selectedScope(): ExportScope | null {
  const selected = scopeInputs.find((input) => input.checked && !input.disabled)
  return selected ? (selected.value as ExportScope) : null
}

function updateExportButton() {
  exportButton.disabled =
    !ready || exporting || !privacyConfirm.checked || !selectedScope()
}

function setExporting(value: boolean) {
  exporting = value
  exportCard.setAttribute('aria-busy', String(value))
  scopeFieldset.disabled = value || !ready
  privacyConfirm.disabled = value || !ready
  exportButton.textContent = loadTimeData.getString(
    value ? 'diagnosticsExportingButton' : 'diagnosticsExportButton',
  )
  updateExportButton()
}

function hideStatus() {
  exportStatus.hidden = true
  outputPath.hidden = true
  outputPath.textContent = ''
}

function showStatus(
  state: StatusState,
  title: string,
  message: string,
  path = '',
  focus = true,
) {
  exportStatus.dataset.state = state
  exportStatus.setAttribute(
    'aria-live',
    state === 'error' ? 'assertive' : 'polite',
  )
  statusIcon.textContent =
    state === 'success'
      ? '✓'
      : state === 'error'
        ? '!'
        : state === 'cancelled'
          ? '—'
          : ''
  statusTitle.textContent = title
  statusMessage.textContent = message
  outputPath.textContent = path
  outputPath.hidden = !path
  exportStatus.hidden = false
  if (focus) {
    exportStatus.focus()
  }
}

function asRecord(value: unknown): Record<string, unknown> | null {
  if (!value || typeof value !== 'object' || Array.isArray(value)) {
    return null
  }
  return value as Record<string, unknown>
}

function applyDiagnosticsState(state: DiagnosticsState) {
  const defaultInput = scopeInputs.find(
    (input) => input.value === state.defaultScope,
  )
  if (defaultInput) {
    defaultInput.checked = true
  }

  openCrashFolder.disabled = !state.crashFolderAvailable
  openExportFolder.disabled = !state.lastExportAvailable

  ready = state.canExport && !state.exporting
  scopeFieldset.disabled = !ready
  privacyConfirm.disabled = !ready
  if (state.exporting) {
    availability.textContent = loadTimeData.getString(
      'diagnosticsExportAlreadyRunning',
    )
  } else if (!state.canExport) {
    availability.textContent = state.guest
      ? loadTimeData.getString('diagnosticsUnavailableGuest')
      : loadTimeData.getString('diagnosticsUnavailableProfile')
  } else {
    availability.textContent = loadTimeData.getStringF(
      'diagnosticsAvailableReports',
      state.localCrashCount,
    )
  }
  updateExportButton()
}

function cancellationReason(value: unknown): boolean {
  if (typeof value === 'string') {
    return /cancel(?:led|ed)/i.test(value)
  }
  const result = asRecord(value)
  return Boolean(
    result?.cancelled
      || (typeof result?.error === 'string'
        && /cancel(?:led|ed)/i.test(result.error)),
  )
}

function errorText(value: unknown): string {
  const result = asRecord(value)
  const source =
    typeof result?.error === 'string'
      ? result.error
      : value instanceof Error
        ? value.message
        : typeof value === 'string'
          ? value
          : ''
  const errorCode = source.replace(/^Error:\s*/i, '').split(':', 1)[0]
  const knownMessages: Record<string, string> = {
    archive_size_limit_exceeded: '诊断包超过大小限制',
    export_already_running: '另一个诊断导出任务正在运行',
    forbidden_text_detected: '诊断信息脱敏检查未通过',
    invalid_scope: '选择的时间范围无效',
    profile_not_allowed: '当前用户配置文件不允许导出诊断信息',
    profile_unavailable: '当前用户配置文件不可用',
    save_dialog_unavailable: '无法打开文件保存窗口',
  }
  return (
    knownMessages[errorCode]
    || loadTimeData.getString('diagnosticsUnknownError')
  )
}

function successMessage(result: DiagnosticsExportResult): string {
  const message = loadTimeData.getStringF(
    'diagnosticsExportCompleteMessage',
    result.crashCount,
  )
  const omitted = result.omittedCrashCount
    ? ` ${loadTimeData.getStringF(
        'diagnosticsReportsOmitted',
        result.omittedCrashCount,
      )}`
    : ''
  return `${message}${omitted}`
}

async function loadDiagnosticsState() {
  try {
    const state = await sendWithPromise<DiagnosticsState>('getDiagnosticsState')
    applyDiagnosticsState(state)
  } catch (error) {
    availability.textContent = loadTimeData.getString('diagnosticsLoadFailed')
    showStatus(
      'error',
      loadTimeData.getString('diagnosticsLoadFailedTitle'),
      `${errorText(error)}. ${loadTimeData.getString('diagnosticsReloadToRetry')}`,
    )
  }
}

async function exportBundle() {
  const scope = selectedScope()
  if (!scope || !privacyConfirm.checked || exporting) {
    privacyConfirm.focus()
    return
  }

  setExporting(true)
  showStatus(
    'progress',
    loadTimeData.getString('diagnosticsCreatingTitle'),
    loadTimeData.getString('diagnosticsCreatingMessage'),
    '',
    false,
  )

  try {
    const value = await sendWithPromise<DiagnosticsExportResult>(
      'exportDiagnosticsBundle',
      scope,
    )
    if (cancellationReason(value)) {
      showStatus(
        'cancelled',
        loadTimeData.getString('diagnosticsExportCanceledTitle'),
        loadTimeData.getString('diagnosticsExportCanceledMessage'),
      )
      return
    }
    if (!value.success) {
      throw new Error(errorText(value))
    }

    openExportFolder.disabled = false
    showStatus(
      'success',
      loadTimeData.getString('diagnosticsExportCompleteTitle'),
      successMessage(value),
      value.archiveName,
    )
  } catch (error) {
    if (cancellationReason(error)) {
      showStatus(
        'cancelled',
        loadTimeData.getString('diagnosticsExportCanceledTitle'),
        loadTimeData.getString('diagnosticsExportCanceledMessage'),
      )
    } else {
      showStatus(
        'error',
        loadTimeData.getString('diagnosticsExportFailedTitle'),
        `${errorText(error)}. ${loadTimeData.getString('diagnosticsExportFailedMessage')}`,
      )
    }
  } finally {
    privacyConfirm.checked = false
    setExporting(false)
  }
}

privacyConfirm.addEventListener('change', updateExportButton)
scopeInputs.forEach((input) => {
  input.addEventListener('change', () => {
    privacyConfirm.checked = false
    hideStatus()
    updateExportButton()
  })
})
exportButton.addEventListener('click', exportBundle)
openCrashFolder.addEventListener('click', () => {
  chrome.send('openDiagnosticsCrashFolder')
  folderStatus.textContent = loadTimeData.getString(
    'diagnosticsOpeningCrashFolder',
  )
})
openExportFolder.addEventListener('click', () => {
  chrome.send('openDiagnosticsExportFolder')
  folderStatus.textContent = loadTimeData.getString(
    'diagnosticsOpeningExportFolder',
  )
})

loadDiagnosticsState()
