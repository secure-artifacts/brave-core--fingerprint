import { combinedSurfaceHash, sha256Hex } from './surface-hash.js'

function bytesOfFloat32(values) {
  return new Uint8Array(values.buffer, values.byteOffset, values.byteLength)
}

async function canvasProbe() {
  const stableCanvas = document.createElement('canvas')
  stableCanvas.width = 320
  stableCanvas.height = 80
  const stableContext = stableCanvas.getContext('2d')
  stableContext.fillStyle = '#f60'
  stableContext.fillRect(12, 10, 90, 42)
  stableContext.font = '18px Arial'
  stableContext.fillStyle = '#069'
  stableContext.fillText('Surface stability 0123456789', 4, 52)
  const stableRead1 = stableContext.getImageData(
    0,
    0,
    stableCanvas.width,
    stableCanvas.height,
  ).data
  const stableRead2 = stableContext.getImageData(
    0,
    0,
    stableCanvas.width,
    stableCanvas.height,
  ).data
  const [stableHash1, stableHash2] = await Promise.all([
    sha256Hex(stableRead1),
    sha256Hex(stableRead2),
  ])

  const size = 8
  const source = document.createElement('canvas')
  const mirror = document.createElement('canvas')
  source.width = mirror.width = size
  source.height = mirror.height = size
  const options = { desynchronized: true, willReadFrequently: true }
  const sourceContext = source.getContext('2d', options)
  const mirrorContext = mirror.getContext('2d', options)
  const random = crypto.getRandomValues(new Uint8Array(size * size * 3))
  const original = new Uint8Array(size * size * 4)
  for (let y = 0; y < size; y += 1) {
    for (let x = 0; x < size; x += 1) {
      const offset = (y * size + x) * 3
      const originalOffset = (y * size + x) * 4
      original.set(random.subarray(offset, offset + 3), originalOffset)
      original[originalOffset + 3] = 255
      sourceContext.fillStyle = `rgb(${random[offset]},${random[offset + 1]},${random[offset + 2]})`
      sourceContext.fillRect(x, y, 1, 1)
    }
  }

  const noise = []
  const modifiedChannels = new Set()
  let modifiedPixels = 0
  for (let y = 0; y < size; y += 1) {
    for (let x = 0; x < size; x += 1) {
      const first = sourceContext.getImageData(x, y, 1, 1).data
      mirrorContext.fillStyle = `rgba(${first[0]},${first[1]},${first[2]},${first[3] / 255})`
      mirrorContext.fillRect(x, y, 1, 1)
      const second = mirrorContext.getImageData(x, y, 1, 1).data
      const changed = []
      for (let channel = 0; channel < 4; channel += 1) {
        if (first[channel] !== second[channel]) {
          changed.push([channel, second[channel]])
        }
      }
      if (changed.length > 0) noise.push([x, y, changed])

      const originalOffset = (y * size + x) * 4
      let pixelModified = false
      for (let channel = 0; channel < 4; channel += 1) {
        if (original[originalOffset + channel] === second[channel]) continue
        modifiedChannels.add('rgba'[channel])
        pixelModified = true
      }
      if (pixelModified) modifiedPixels += 1
    }
  }
  const modsSignature = `${[...modifiedChannels].sort().join('')}:${modifiedPixels}`

  const visualization = document.querySelector('#canvas-visualization')
  const visualizationContext = visualization.getContext('2d')
  visualizationContext.fillStyle = '#fff'
  visualizationContext.fillRect(0, 0, 40, 40)
  for (const [x, y, changed] of noise) {
    const channels = [255, 255, 255, 255]
    for (const [channel, value] of changed) channels[channel] = value
    visualizationContext.fillStyle = `rgba(${channels[0]},${channels[1]},${channels[2]},${channels[3] / 255})`
    visualizationContext.fillRect(x * 5, y * 5, 5, 5)
  }
  const visualizationBytes = visualizationContext.getImageData(
    0,
    0,
    visualization.width,
    visualization.height,
  ).data

  return {
    hash: stableHash1,
    height: stableCanvas.height,
    modsHash: await sha256Hex(new TextEncoder().encode(modsSignature)),
    modsSignature,
    repeatStable: stableHash1 === stableHash2,
    visualizationHash: await sha256Hex(visualizationBytes),
    width: stableCanvas.width,
  }
}

async function audioProbe() {
  const context = new OfflineAudioContext(1, 5000, 44100)
  const oscillator = context.createOscillator()
  const compressor = context.createDynamicsCompressor()
  oscillator.type = 'triangle'
  oscillator.frequency.value = 10000
  compressor.threshold.value = -50
  compressor.knee.value = 40
  compressor.attack.value = 0
  oscillator.connect(compressor)
  compressor.connect(context.destination)
  oscillator.start(0)
  const buffer = await context.startRendering()
  const copied = new Float32Array(buffer.length)
  buffer.copyFromChannel(copied, 0)
  const direct1 = buffer.getChannelData(0)
  const direct2 = buffer.getChannelData(0)
  const sample = direct1.slice(4500, 5000)

  const trapBuffer = new AudioBuffer({ length: 2000, sampleRate: 44100 })
  const trap = crypto.getRandomValues(new Uint32Array(1))[0] / 0xffffffff
  const trapView = trapBuffer.getChannelData(0)
  for (const index of [275, 285, 295]) trapView[index] = trap
  const trapCopy = new Float32Array(trapBuffer.length)
  trapBuffer.copyFromChannel(trapCopy, 0)
  const mismatches = []
  const trapIndexes = [275, 285, 295]
  const trapValues = []
  for (const index of trapIndexes) {
    const direct = trapBuffer.getChannelData(0)[index]
    trapValues.push(direct)
    if (direct !== trapCopy[index])
      mismatches.push([index, direct, trapCopy[index]])
  }
  const trapStable = new Set(trapValues).size === 1
  const trapNoise = trapStable
    ? 0
    : [...new Set(trapValues)].reduce((sum, value) => sum + value, 0)

  const [hash, directHash, copiedHash, noiseHash] = await Promise.all([
    sha256Hex(bytesOfFloat32(sample)),
    sha256Hex(bytesOfFloat32(direct2)),
    sha256Hex(bytesOfFloat32(copied)),
    sha256Hex(new TextEncoder().encode(String(trapNoise))),
  ])
  return {
    hash,
    noiseHash,
    repeatStable: directHash === copiedHash && mismatches.length === 0,
    sampleCount: sample.length,
    trapStable,
  }
}

try {
  const [canvas, audio] = await Promise.all([canvasProbe(), audioProbe()])
  window.__fpSurfaceStabilityResult = {
    audio,
    canvas,
    combinedHash: await combinedSurfaceHash(canvas.hash, audio.hash),
  }
  window.__fpSurfaceStabilityReady = true
} catch (error) {
  window.__fpSurfaceStabilityError = error?.stack || String(error)
}
