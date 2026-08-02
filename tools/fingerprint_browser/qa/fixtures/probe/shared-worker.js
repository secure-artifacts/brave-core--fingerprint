// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.
self.onconnect = (event) => {
  const port = event.ports[0]
  port.onmessage = () =>
    port.postMessage({
      hardwareConcurrency: navigator.hardwareConcurrency,
      language: navigator.language,
      languages: [...navigator.languages],
      platform: navigator.platform,
      userAgent: navigator.userAgent,
    })
  port.start()
}
