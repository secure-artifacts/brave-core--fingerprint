// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.
self.addEventListener('install', () => self.skipWaiting())
self.addEventListener('activate', (event) =>
  event.waitUntil(self.clients.claim()),
)
self.addEventListener('message', (event) =>
  event.ports[0].postMessage({
    hardwareConcurrency: navigator.hardwareConcurrency,
    language: navigator.language,
    languages: [...navigator.languages],
    platform: navigator.platform,
    userAgent: navigator.userAgent,
  }),
)
