// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.
chrome.runtime.onInstalled.addListener(() => {
  chrome.storage.local.set({ installed: true })
})

function collect() {
  let canvas = null
  try {
    const target = new OffscreenCanvas(64, 32)
    const context = target.getContext('2d')
    context.fillText('Fingerprint QA', 2, 18)
    canvas = [...context.getImageData(0, 0, 64, 32).data].reduce(
      (sum, value) => (sum + value) >>> 0,
      0,
    )
  } catch (error) {
    canvas = `unavailable: ${error}`
  }
  return {
    canvas,
    hardwareConcurrency: navigator.hardwareConcurrency,
    language: navigator.language,
    languages: [...navigator.languages],
    platform: navigator.platform,
    userAgent: navigator.userAgent,
  }
}

chrome.runtime.onMessage.addListener((message, _sender, sendResponse) => {
  if (message?.type === 'collect') {
    sendResponse(collect())
    return
  }
  if (message?.type === 'set-proxy-conflict') {
    chrome.proxy.settings.set(
      {
        scope: 'regular',
        value: {
          mode: 'fixed_servers',
          rules: {
            singleProxy: { host: '127.0.0.1', port: 9, scheme: 'http' },
          },
        },
      },
      () => sendResponse({ error: chrome.runtime.lastError?.message || null }),
    )
    return true
  }
  if (message?.type === 'clear-proxy-conflict') {
    chrome.proxy.settings.clear({ scope: 'regular' }, () =>
      sendResponse({ error: chrome.runtime.lastError?.message || null }),
    )
    return true
  }
})
