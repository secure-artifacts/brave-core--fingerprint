function hash(value) {
  let result = 2166136261
  for (let index = 0; index < value.length; index += 1) {
    result ^= value.charCodeAt(index)
    result = Math.imul(result, 16777619)
  }
  return (result >>> 0).toString(16).padStart(8, '0')
}

function hashBytes(bytes) {
  let result = 2166136261
  for (const value of bytes) {
    result ^= value
    result = Math.imul(result, 16777619)
  }
  return (result >>> 0).toString(16).padStart(8, '0')
}

function basic(scope = globalThis) {
  const navigator = scope.navigator
  return {
    deviceMemory: navigator.deviceMemory ?? null,
    hardwareConcurrency: navigator.hardwareConcurrency,
    language: navigator.language,
    languages: [...navigator.languages],
    platform: navigator.platform,
    touch: navigator.maxTouchPoints,
    userAgent: navigator.userAgent,
  }
}

async function canvasFingerprint() {
  const canvas = document.createElement('canvas')
  canvas.width = 320
  canvas.height = 80
  const context = canvas.getContext('2d')
  context.fillStyle = '#f60'
  context.fillRect(12, 10, 90, 42)
  context.font = '18px Arial'
  context.fillStyle = '#069'
  context.fillText('Brave fingerprint QA 0123456789', 4, 52)
  const pixels = context.getImageData(0, 0, canvas.width, canvas.height).data
  const dataUrl = canvas.toDataURL()
  const blob = await new Promise(resolve => canvas.toBlob(resolve, 'image/png'))
  return {
    blob: blob ? hashBytes(new Uint8Array(await blob.arrayBuffer())) : null,
    dataUrl: hash(dataUrl),
    pixels: hashBytes(pixels),
  }
}

function webglFingerprint() {
  const gl = document.createElement('canvas').getContext('webgl')
  if (!gl) return null
  const extension = gl.getExtension('WEBGL_debug_renderer_info')
  gl.clearColor(0.2, 0.4, 0.6, 1)
  gl.clear(gl.COLOR_BUFFER_BIT)
  const pixels = new Uint8Array(4)
  gl.readPixels(0, 0, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixels)
  return {
    pixels: hashBytes(pixels),
    renderer: extension ? gl.getParameter(extension.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER),
    vendor: extension ? gl.getParameter(extension.UNMASKED_VENDOR_WEBGL) : gl.getParameter(gl.VENDOR),
  }
}

function fontFingerprint() {
  const canvas = document.createElement('canvas')
  const context = canvas.getContext('2d')
  const fonts = ['Arial', 'Courier New', 'Georgia', 'Helvetica', 'Times New Roman']
  return Object.fromEntries(fonts.map(font => {
    context.font = `72px ${font}`
    return [font, context.measureText('Brave fingerprint QA').width]
  }))
}

async function audioFingerprint() {
  const context = new OfflineAudioContext(1, 44100, 44100)
  const oscillator = context.createOscillator()
  const compressor = context.createDynamicsCompressor()
  oscillator.type = 'triangle'
  oscillator.frequency.value = 10000
  oscillator.connect(compressor)
  compressor.connect(context.destination)
  oscillator.start(0)
  const buffer = await context.startRendering()
  const samples = buffer.getChannelData(0).slice(4500, 5000)
  return hash([...samples].map(value => value.toFixed(8)).join(','))
}

async function userAgentData() {
  if (!navigator.userAgentData) return null
  return await navigator.userAgentData.getHighEntropyValues([
    'architecture', 'bitness', 'fullVersionList', 'model', 'platform', 'platformVersion', 'wow64',
  ])
}

async function webgpuFingerprint() {
  if (!navigator.gpu) return {available: false}
  const adapter = await navigator.gpu.requestAdapter()
  if (!adapter) return {available: true, adapter: null}
  const info = adapter.info || {}
  return {
    adapter: {
      architecture: info.architecture || '',
      description: info.description || '',
      device: info.device || '',
      vendor: info.vendor || '',
    },
    available: true,
    features: [...adapter.features].sort(),
  }
}

async function localFonts() {
  if (!globalThis.queryLocalFonts) return {available: false}
  try {
    const permission = await navigator.permissions.query({name: 'local-fonts'})
    if (permission.state !== 'granted') {
      return {available: true, permission: permission.state}
    }
    const fonts = await globalThis.queryLocalFonts()
    return {
      available: true,
      families: [...new Set(fonts.map(font => font.family))].sort(),
      permission: permission.state,
    }
  } catch (error) {
    return {available: true, error: String(error)}
  }
}

async function geolocation() {
  return await new Promise(resolve => {
    if (!navigator.geolocation) {
      resolve({available: false})
      return
    }
    navigator.geolocation.getCurrentPosition(
      position => resolve({
        accuracy: position.coords.accuracy,
        latitude: position.coords.latitude,
        longitude: position.coords.longitude,
      }),
      error => resolve({error: error.message}),
      {enableHighAccuracy: false, maximumAge: 0, timeout: 5000},
    )
  })
}

async function dedicatedWorker() {
  return await new Promise((resolve, reject) => {
    const worker = new Worker('dedicated-worker.js')
    worker.onmessage = event => {
      worker.terminate()
      resolve(event.data)
    }
    worker.onerror = reject
    worker.postMessage('collect')
  })
}

async function sharedWorker() {
  return await new Promise((resolve, reject) => {
    try {
      const worker = new SharedWorker('shared-worker.js')
      worker.port.onmessage = event => resolve(event.data)
      worker.onerror = reject
      worker.port.start()
      worker.port.postMessage('collect')
    } catch (error) {
      resolve({error: String(error)})
    }
  })
}

async function serviceWorker() {
  if (!('serviceWorker' in navigator)) return {available: false}
  await navigator.serviceWorker.register('service-worker.js', {scope: '/'})
  await navigator.serviceWorker.ready
  const registration = await navigator.serviceWorker.getRegistration('/')
  const worker = registration?.active
  if (!worker) return {error: 'no active worker'}
  return await new Promise(resolve => {
    const channel = new MessageChannel()
    channel.port1.onmessage = event => resolve(event.data)
    worker.postMessage('collect', [channel.port2])
  })
}

async function collect() {
  const iframe = document.createElement('iframe')
  iframe.src = 'iframe.html'
  iframe.hidden = true
  document.body.append(iframe)
  await new Promise(resolve => iframe.addEventListener('load', resolve, {once: true}))
  const result = {
    audio: await audioFingerprint(),
    basic: basic(),
    canvas: await canvasFingerprint(),
    dedicatedWorker: await dedicatedWorker(),
    fonts: fontFingerprint(),
    geolocation: await geolocation(),
    iframe: iframe.contentWindow.collectBasic(),
    localFonts: await localFonts(),
    screen: {
      availHeight: screen.availHeight,
      availWidth: screen.availWidth,
      colorDepth: screen.colorDepth,
      height: screen.height,
      pixelDepth: screen.pixelDepth,
      width: screen.width,
    },
    serviceWorker: await serviceWorker(),
    sharedWorker: await sharedWorker(),
    timezone: Intl.DateTimeFormat().resolvedOptions().timeZone,
    uaData: await userAgentData(),
    webgl: webglFingerprint(),
    webgpu: await webgpuFingerprint(),
  }
  window.__fpQaResult = result
  window.__fpQaReady = true
  document.querySelector('#status').textContent = 'Ready'
  document.querySelector('#result').textContent = JSON.stringify(result, null, 2)
}

collect().catch(error => {
  window.__fpQaError = String(error?.stack || error)
  document.querySelector('#status').textContent = 'Failed'
  document.querySelector('#result').textContent = window.__fpQaError
})
