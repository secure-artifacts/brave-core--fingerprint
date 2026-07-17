self.onconnect = event => {
  const port = event.ports[0]
  port.onmessage = () => port.postMessage({
    hardwareConcurrency: navigator.hardwareConcurrency,
    language: navigator.language,
    languages: [...navigator.languages],
    platform: navigator.platform,
    userAgent: navigator.userAgent,
  })
  port.start()
}
