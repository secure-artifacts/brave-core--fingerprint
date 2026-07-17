self.addEventListener('install', () => self.skipWaiting())
self.addEventListener('activate', event => event.waitUntil(self.clients.claim()))
self.addEventListener('message', event => event.ports[0].postMessage({
  hardwareConcurrency: navigator.hardwareConcurrency,
  language: navigator.language,
  languages: [...navigator.languages],
  platform: navigator.platform,
  userAgent: navigator.userAgent,
}))
