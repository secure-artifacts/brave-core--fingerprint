// Copyright (c) 2026 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.
import fs from 'node:fs/promises'
import http from 'node:http'
import path from 'node:path'

const CONTENT_TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
}

const CLIENT_HINTS = [
  'Sec-CH-UA-Arch',
  'Sec-CH-UA-Bitness',
  'Sec-CH-UA-Full-Version-List',
  'Sec-CH-UA-Model',
  'Sec-CH-UA-Platform-Version',
  'Sec-CH-UA-WoW64',
].join(', ')

export async function startProbeServer(root) {
  const requests = []
  const server = http.createServer(async (request, response) => {
    requests.push({
      method: request.method,
      time: new Date().toISOString(),
      url: request.url,
    })
    const requestPath = new URL(request.url, 'http://127.0.0.1').pathname
    if (requestPath === '/headers.json') {
      response.writeHead(200, {
        'Accept-CH': CLIENT_HINTS,
        'Cache-Control': 'no-store',
        'Content-Type': 'application/json; charset=utf-8',
      })
      response.end(JSON.stringify(request.headers))
      return
    }
    const relative = requestPath === '/' ? 'index.html' : requestPath.slice(1)
    const target = path.resolve(root, relative)
    if (!target.startsWith(`${path.resolve(root)}${path.sep}`)) {
      response.writeHead(403).end('forbidden')
      return
    }
    try {
      const content = await fs.readFile(target)
      response.writeHead(200, {
        'Accept-CH': CLIENT_HINTS,
        'Cache-Control': 'no-store',
        'Content-Type':
          CONTENT_TYPES[path.extname(target)] || 'application/octet-stream',
        'Service-Worker-Allowed': '/',
      })
      response.end(content)
    } catch {
      response.writeHead(404).end('not found')
    }
  })
  await new Promise((resolve, reject) => {
    server.once('error', reject)
    server.listen(0, '127.0.0.1', resolve)
  })
  const address = server.address()
  return {
    origin: `http://127.0.0.1:${address.port}`,
    requests,
    async close() {
      await new Promise((resolve, reject) =>
        server.close((error) => (error ? reject(error) : resolve())),
      )
    },
  }
}
