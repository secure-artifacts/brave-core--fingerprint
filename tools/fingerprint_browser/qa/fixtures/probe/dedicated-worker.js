self.onmessage = () => self.postMessage({
  hardwareConcurrency: navigator.hardwareConcurrency,
  language: navigator.language,
  languages: [...navigator.languages],
  platform: navigator.platform,
  userAgent: navigator.userAgent,
})
