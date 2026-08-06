// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.
const encoder = new TextEncoder()

export async function sha256Hex(bytes) {
  const view = ArrayBuffer.isView(bytes)
    ? new Uint8Array(bytes.buffer, bytes.byteOffset, bytes.byteLength)
    : new Uint8Array(bytes)
  const digest = await crypto.subtle.digest('SHA-256', view)
  return [...new Uint8Array(digest)]
    .map((value) => value.toString(16).padStart(2, '0'))
    .join('')
}

export async function combinedSurfaceHash(canvasHash, audioHash) {
  return await sha256Hex(encoder.encode(`${canvasHash}\0${audioHash}`))
}
