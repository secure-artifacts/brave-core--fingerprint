import {createHash} from 'node:crypto'
import {createReadStream} from 'node:fs'
import fs from 'node:fs/promises'
import path from 'node:path'

import {pathExists, run, sha256} from './system.mjs'

const SOURCE_GROUPS = {
  native: [
    '../chrome/browser/net/proxy_config_monitor.cc',
    '../chrome/browser/net/profile_network_context_service.cc',
    '../components/performance_manager/worker_watcher.cc',
    '../net/http/http_network_transaction.cc',
    '../net/http/http_network_transaction.h',
    '../net/base/proxy_chain.cc',
    '../net/base/proxy_server.cc',
    '../net/proxy_resolution/proxy_list.cc',
    '../third_party/blink/renderer/core/html/canvas/canvas_async_blob_creator.cc',
    '../third_party/blink/renderer/core/workers/shared_worker_content_settings_proxy.cc',
    '../third_party/blink/renderer/modules/service_worker/service_worker_content_settings_proxy.cc',
    '../third_party/blink/renderer/modules/speech/speech_synthesis.cc',
    '../third_party/blink/renderer/modules/speech/speech_synthesis.h',
    '../third_party/blink/renderer/modules/webgl/webgl_rendering_context_base.cc',
    'browser/brave_content_browser_client.cc',
    'browser/brave_content_browser_client.h',
    'browser/farbling',
    'browser/fingerprint_browser',
    'browser/ui/views/toolbar/brave_toolbar_view.cc',
    'browser/ui/views/toolbar/brave_toolbar_view.h',
    'browser/ui/views/toolbar/fingerprint_proxy_button.cc',
    'browser/ui/views/toolbar/fingerprint_proxy_button.h',
    'browser/ui/webui/brave_settings_ui.cc',
    'browser/ui/webui/fingerprint_test',
    'browser/ui/webui/settings/fingerprint_profile_proxy_handler.cc',
    'browser/ui/webui/settings/fingerprint_profile_proxy_handler.h',
    'chromium_src/chrome/browser/content_settings',
    'chromium_src/chrome/renderer/worker_content_settings_client.cc',
    'chromium_src/net/base/host_port_pair.cc',
    'chromium_src/net/base/host_port_pair.h',
    'chromium_src/net/base/proxy_string_util.cc',
    'chromium_src/net/base/proxy_string_util.h',
    'chromium_src/components/content_settings',
    'chromium_src/content/browser/service_worker',
    'chromium_src/content/browser/worker_host',
    'chromium_src/content/public/browser/content_browser_client.cc',
    'chromium_src/content/public/browser/content_browser_client.h',
    'chromium_src/third_party/blink/public/mojom/worker/worker_content_settings_proxy.mojom',
    'chromium_src/third_party/blink/renderer/core/html/canvas/canvas_async_blob_creator.cc',
    'chromium_src/third_party/blink/renderer/core/workers/shared_worker_content_settings_proxy.cc',
    'chromium_src/third_party/blink/renderer/modules/service_worker/service_worker_content_settings_proxy.cc',
    'chromium_src/third_party/blink/renderer/modules/speech',
    'chromium_src/third_party/blink/renderer/modules/webgl/webgl_rendering_context_base.cc',
    'components/content_settings/renderer/brave_content_settings_agent_impl.cc',
    'components/fingerprint_browser/browser',
    'net/proxy_resolution/profile_proxy_config_service.cc',
    'net/proxy_resolution/profile_proxy_config_service.h',
    'patches/chrome-browser-net-proxy_config_monitor.cc.patch',
    'patches/chrome-browser-net-profile_network_context_service.cc.patch',
    'patches/components-performance_manager-worker_watcher.cc.patch',
    'patches/net-http-http_network_transaction.cc.patch',
    'patches/net-http-http_network_transaction.h.patch',
    'patches/net-base-proxy_chain.cc.patch',
    'patches/net-base-proxy_server.cc.patch',
    'patches/net-proxy_resolution-proxy_list.cc.patch',
    'patches/third_party-blink-renderer-core-BUILD.gn.patch',
    'patches/third_party-blink-renderer-core-execution_context-navigator_base.cc.patch',
    'patches/third_party-blink-renderer-core-frame-navigator_device_memory.cc.patch',
    'patches/third_party-blink-renderer-modules-font_access-font_metadata.cc.patch',
    'patches/third_party-blink-renderer-modules-plugins-dom_plugin_array.cc.patch',
    'patches/third_party-blink-renderer-modules-webgl-webgl_rendering_context_base.cc.patch',
    'third_party/blink/renderer/core/farbling/brave_session_cache.cc',
    'third_party/libmaxminddb/include/maxminddb_config.h',
  ],
  braveResources: [
    'app/brave_settings_strings.grdp',
    'browser/resources/settings/fingerprint_profile_proxy_page',
    'browser/resources/settings/br/privacy_page_index.ts',
    'components/fingerprint_browser/resources',
  ],
  localeResources: [
    'app/brave_settings_strings.grdp',
  ],
  scaledResources: [
    'app/brave_settings_strings.grdp',
    'components/fingerprint_browser/resources/fingerprint_proxy_flag_resources.grdp',
    'components/resources/brave_components_resources.grd',
  ],
  chromiumResources: [
    'chromium_src/ui/webui/resources/cr_elements/md_select_lit.css',
  ],
}

const SCALED_RESOURCE_PACKS = [
  'brave_100_percent.pak',
  'brave_200_percent.pak',
  'chrome_100_percent.pak',
  'chrome_200_percent.pak',
]

const BUILD_MANIFEST_NAME = 'fingerprint-browser-build-manifest.json'

async function filesUnder(target) {
  const stat = await fs.stat(target)
  if (stat.isFile()) {
    return [target]
  }
  const files = []
  for (const entry of await fs.readdir(target, {withFileTypes: true})) {
    const child = path.join(target, entry.name)
    if (entry.isDirectory()) {
      files.push(...await filesUnder(child))
    } else if (entry.isFile()) {
      files.push(child)
    }
  }
  return files
}

async function namedFilesUnder(target, name) {
  return (await filesUnder(target)).filter(file => path.basename(file) === name)
}

async function latestSource(braveRoot, paths) {
  const relevant = await sourceFiles(braveRoot, paths)
  let latest = {file: null, mtimeMs: 0}
  for (const file of relevant) {
    const stat = await fs.stat(file)
    if (stat.mtimeMs > latest.mtimeMs) {
      latest = {file, mtimeMs: stat.mtimeMs}
    }
  }
  return latest
}

async function sourceFiles(braveRoot, paths) {
  const files = []
  for (const relative of paths) {
    const target = path.join(braveRoot, relative)
    if (await pathExists(target)) {
      files.push(...await filesUnder(target))
    }
  }
  const relevant = files.filter(file =>
    !file.includes(`${path.sep}test${path.sep}`) &&
    !file.endsWith('_browsertest.cc') &&
    !file.endsWith('_unittest.cc'))
  return [...new Set(relevant)].sort()
}

async function sourceGroupIdentity(braveRoot, groupName) {
  const files = await sourceFiles(braveRoot, SOURCE_GROUPS[groupName])
  const digest = createHash('sha256')
  const records = []
  for (const file of files) {
    const relative = path.relative(braveRoot, file)
    const contentSha256 = await sha256(file)
    records.push({file: relative, sha256: contentSha256})
    digest.update(`${relative}\0${contentSha256}\n`)
  }
  return {
    count: records.length,
    digest: digest.digest('hex'),
    files: records,
  }
}

async function findFramework(outDir) {
  const entries = await fs.readdir(outDir, {withFileTypes: true})
  const entry = entries.find(candidate =>
    candidate.isDirectory() && candidate.name.endsWith(' Framework.framework'))
  if (!entry) {
    throw new Error(`No unpacked framework found in ${outDir}`)
  }
  return path.join(outDir, entry.name)
}

async function frameworkVersionDirectory(framework) {
  const versions = path.join(framework, 'Versions')
  const current = path.join(versions, 'Current')
  if (await pathExists(current)) {
    return await fs.realpath(current)
  }
  const entries = await fs.readdir(versions, {withFileTypes: true})
  const version = entries.find(entry => entry.isDirectory())
  if (!version) {
    throw new Error(`No framework version found in ${framework}`)
  }
  return path.join(versions, version.name)
}

async function dylibsIn(directory) {
  return (await fs.readdir(directory, {withFileTypes: true}))
    .filter(entry => entry.isFile() && entry.name.endsWith('.dylib'))
    .map(entry => path.join(directory, entry.name))
    .sort()
}

async function localePacksIn(directory) {
  return (await fs.readdir(directory, {withFileTypes: true}))
    .filter(entry => entry.isFile() && entry.name.endsWith('.pak'))
    .map(entry => path.join(directory, entry.name))
    .sort()
}

async function fileRecord(file) {
  const stat = await fs.stat(file)
  return {
    file,
    mtime: stat.mtime.toISOString(),
    mtimeMs: stat.mtimeMs,
    sha256: await sha256(file),
    size: stat.size,
  }
}

async function updateHashFromRange(hash, file, start, end) {
  if (end <= start) return
  for await (const chunk of createReadStream(file, {start, end: end - 1})) {
    hash.update(chunk)
  }
}

export async function unsignedMachOSha256(file) {
  const handle = await fs.open(file, 'r')
  try {
    const header = Buffer.alloc(32)
    const {bytesRead} = await handle.read(header, 0, header.length, 0)
    if (bytesRead !== header.length) {
      throw new Error(`Mach-O header is truncated: ${file}`)
    }

    const magic = header.readUInt32LE(0)
    let readUInt32
    if (magic === 0xfeedfacf) {
      readUInt32 = (buffer, offset) => buffer.readUInt32LE(offset)
    } else if (magic === 0xcffaedfe) {
      readUInt32 = (buffer, offset) => buffer.readUInt32BE(offset)
    } else {
      throw new Error(`Unsupported Mach-O format for content hashing: ${file}`)
    }

    const commandCount = readUInt32(header, 16)
    const commandBytes = readUInt32(header, 20)
    const commands = Buffer.alloc(commandBytes)
    const commandRead = await handle.read(commands, 0, commandBytes, header.length)
    if (commandRead.bytesRead !== commandBytes) {
      throw new Error(`Mach-O load commands are truncated: ${file}`)
    }

    let cursor = 0
    const normalizedFields = []
    let signature = null
    for (let index = 0; index < commandCount; index += 1) {
      if (cursor + 8 > commands.length) {
        throw new Error(`Mach-O load command ${index} is truncated: ${file}`)
      }
      const command = readUInt32(commands, cursor)
      const commandSize = readUInt32(commands, cursor + 4)
      if (commandSize < 8 || cursor + commandSize > commands.length) {
        throw new Error(`Mach-O load command ${index} is invalid: ${file}`)
      }
      if (command === 0x19 && commandSize >= 72) {
        const segmentName = commands.subarray(cursor + 8, cursor + 24)
          .toString('ascii').replace(/\0.*$/, '')
        if (segmentName === '__LINKEDIT') {
          normalizedFields.push(
            {offset: header.length + cursor + 32, size: 8},
            {offset: header.length + cursor + 48, size: 8},
          )
        }
      }
      if (command === 0x1d) {
        if (commandSize < 16) {
          throw new Error(`Mach-O code signature command is invalid: ${file}`)
        }
        signature = {
          dataOffset: readUInt32(commands, cursor + 8),
        }
        normalizedFields.push({offset: header.length + cursor + 8, size: 8})
      }
      cursor += commandSize
    }

    if (!signature) return await sha256(file)
    const stat = await handle.stat()
    if (signature.dataOffset < header.length + commandBytes ||
        signature.dataOffset > stat.size) {
      throw new Error(`Mach-O code signature offset is invalid: ${file}`)
    }

    const hash = createHash('sha256')
    let rangeStart = 0
    for (const field of normalizedFields.sort((left, right) =>
      left.offset - right.offset)) {
      if (field.offset < rangeStart || field.offset + field.size > signature.dataOffset) {
        throw new Error(`Mach-O normalized field is invalid: ${file}`)
      }
      await updateHashFromRange(hash, file, rangeStart, field.offset)
      hash.update(Buffer.alloc(field.size))
      rangeStart = field.offset + field.size
    }
    await updateHashFromRange(hash, file, rangeStart, signature.dataOffset)
    return hash.digest('hex')
  } finally {
    await handle.close()
  }
}

async function isMachO(file) {
  const handle = await fs.open(file, 'r')
  try {
    const header = Buffer.alloc(4)
    const {bytesRead} = await handle.read(header, 0, header.length, 0)
    if (bytesRead !== header.length) return false
    const littleEndian = header.readUInt32LE(0)
    const bigEndian = header.readUInt32BE(0)
    return littleEndian === 0xfeedfacf || bigEndian === 0xfeedfacf
  } finally {
    await handle.close()
  }
}

async function appExecutableManifest(app) {
  const records = []
  for (const file of await filesUnder(app)) {
    if (file.endsWith('.dylib') || !(await isMachO(file))) continue
    records.push({
      file: path.relative(app, file),
      sha256: await unsignedMachOSha256(file),
    })
  }
  records.sort((left, right) => left.file.localeCompare(right.file))
  const digest = createHash('sha256')
  for (const record of records) {
    digest.update(`${record.file}\0${record.sha256}\n`)
  }
  return {
    count: records.length,
    digest: digest.digest('hex'),
    files: records,
  }
}

async function artifactHashes(outDir) {
  const localeDirectory = path.join(outDir, 'gen', 'repack', 'locales')
  const localeFiles = await localePacksIn(localeDirectory)
  const localeDigest = createHash('sha256')
  for (const file of localeFiles) {
    localeDigest.update(`${path.basename(file)}\0${await sha256(file)}\n`)
  }
  const scaled = {}
  for (const name of SCALED_RESOURCE_PACKS) {
    scaled[name] = await sha256(path.join(outDir, 'gen', 'repack', name))
  }
  return {
    braveResources: await sha256(
      path.join(outDir, 'gen', 'repack', 'brave_resources.pak')),
    chromiumResources: await sha256(
      path.join(outDir, 'gen', 'repack', 'resources.pak')),
    locales: {
      count: localeFiles.length,
      digest: localeDigest.digest('hex'),
    },
    native: await sha256(path.join(outDir, 'libchrome_dll.dylib')),
    scaled,
  }
}

async function readBuildManifest(outDir) {
  const file = path.join(outDir, BUILD_MANIFEST_NAME)
  if (!(await pathExists(file))) return null
  return JSON.parse(await fs.readFile(file, 'utf8'))
}

export async function writeBuildManifest({braveRoot, mode, outDir}) {
  if (mode !== 'cpp' && mode !== 'resources') {
    throw new Error(`Unsupported build manifest mode: ${mode}`)
  }
  const previous = await readBuildManifest(outDir)
  const groups = mode === 'cpp'
    ? {
        braveResources: await sourceGroupIdentity(braveRoot, 'braveResources'),
        chromiumResources: await sourceGroupIdentity(braveRoot, 'chromiumResources'),
        localeResources: await sourceGroupIdentity(braveRoot, 'localeResources'),
        native: await sourceGroupIdentity(braveRoot, 'native'),
        scaledResources: await sourceGroupIdentity(braveRoot, 'scaledResources'),
      }
    : {
        braveResources: await sourceGroupIdentity(braveRoot, 'braveResources'),
        chromiumResources: await sourceGroupIdentity(braveRoot, 'chromiumResources'),
        localeResources: await sourceGroupIdentity(braveRoot, 'localeResources'),
        native: previous?.groups?.native || null,
        scaledResources: await sourceGroupIdentity(braveRoot, 'scaledResources'),
      }
  if (!groups.native) {
    throw new Error('A cpp build manifest is required before a resources-only update')
  }

  const currentArtifacts = await artifactHashes(outDir)
  const artifacts = mode === 'cpp'
    ? currentArtifacts
    : {...currentArtifacts, native: previous.artifacts.native}
  const manifest = {
    artifacts,
    generatedAt: new Date().toISOString(),
    groups,
    mode,
    version: 1,
  }
  const file = path.join(outDir, BUILD_MANIFEST_NAME)
  await fs.writeFile(file, `${JSON.stringify(manifest, null, 2)}\n`)
  return {file, manifest}
}

async function verifyBuildManifest(braveRoot, outDir) {
  const manifest = await readBuildManifest(outDir)
  if (!manifest || manifest.version !== 1) {
    throw new Error('Current build source manifest is missing or unsupported')
  }
  const groups = {}
  for (const groupName of Object.keys(SOURCE_GROUPS)) {
    groups[groupName] = await sourceGroupIdentity(braveRoot, groupName)
    if (groups[groupName].digest !== manifest.groups?.[groupName]?.digest) {
      throw Object.assign(
        new Error(`Build source manifest mismatch for ${groupName}`),
        {details: {
          actual: groups[groupName],
          expected: manifest.groups?.[groupName] || null,
        }})
    }
  }

  const artifacts = await artifactHashes(outDir)
  if (JSON.stringify(artifacts) !== JSON.stringify(manifest.artifacts)) {
    throw Object.assign(new Error('Build artifact hashes do not match source manifest'), {
      details: {actual: artifacts, expected: manifest.artifacts},
    })
  }
  return {
    file: path.join(outDir, BUILD_MANIFEST_NAME),
    generatedAt: manifest.generatedAt,
    groups,
    pass: true,
  }
}

async function unsignedMachOHashMap(files) {
  const hashes = new Map()
  for (const file of files) {
    hashes.set(path.resolve(file), await unsignedMachOSha256(file))
  }
  return hashes
}

async function copyIfDifferent(source, destination) {
  const sourceHash = await sha256(source)
  if (await pathExists(destination)) {
    const destinationHash = await sha256(destination)
    if (sourceHash === destinationHash) {
      return false
    }
  }
  await fs.copyFile(source, destination)
  return true
}

async function uuidMap(files) {
  if (files.length === 0) return new Map()
  const result = await run('dwarfdump', ['--uuid', ...files], {check: true})
  const records = new Map()
  for (const line of result.stdout.split('\n')) {
    const match = line.match(/^UUID: ([0-9A-F-]+) \(([^)]+)\) (.+)$/i)
    if (!match) continue
    const file = path.resolve(match[3])
    const values = records.get(file) || []
    values.push(`${match[2]}:${match[1].toUpperCase()}`)
    records.set(file, values)
  }
  for (const [file, values] of records) {
    records.set(file, values.sort())
  }
  return records
}

async function loadCommandMap(files) {
  if (files.length === 0) return new Map()
  const result = await run('otool', ['-L', ...files], {check: true})
  const records = new Map()
  let current = null
  for (const line of result.stdout.split('\n')) {
    if (!/^\s/.test(line) && line.endsWith(':')) {
      current = path.resolve(line.slice(0, -1))
      records.set(current, [])
      continue
    }
    if (current && line.trim()) {
      records.get(current).push(line.trim())
    }
  }
  return records
}

export async function prepareAndVerifyArtifacts(config, log) {
  const {app, braveRoot, outDir, prepareApp} = config
  const developmentApp = path.join(outDir, 'Brave Browser Development.app')
  if (path.resolve(app) === path.resolve(developmentApp)) {
    throw new Error('QA app must be a dedicated copy, not the build output app')
  }
  if (!(await pathExists(app))) {
    if (!prepareApp) {
      throw new Error(`QA app does not exist: ${app}`)
    }
    if (!(await pathExists(developmentApp))) {
      throw new Error(`Development app baseline does not exist: ${developmentApp}`)
    }
    await fs.mkdir(path.dirname(app), {recursive: true})
    await run('ditto', [developmentApp, app], {check: true})
    await log(`Created dedicated QA app: ${app}`)
  }

  const sourceFramework = await findFramework(outDir)
  const sourceAppExecutables = await appExecutableManifest(developmentApp)
  let appExecutables = await appExecutableManifest(app)
  let baseAppRefreshed = false
  if (sourceAppExecutables.digest !== appExecutables.digest) {
    if (!prepareApp) {
      throw Object.assign(
        new Error('QA app executable set does not match current development app'),
        {details: {app: appExecutables, source: sourceAppExecutables}})
    }
    await fs.rm(app, {recursive: true, force: true})
    await run('ditto', [developmentApp, app], {check: true})
    baseAppRefreshed = true
    appExecutables = await appExecutableManifest(app)
    await log('Refreshed QA app executable, Framework, and Helper baseline')
  }
  if (sourceAppExecutables.digest !== appExecutables.digest) {
    throw Object.assign(
      new Error('QA app executable refresh did not match development output'),
      {details: {app: appExecutables, source: sourceAppExecutables}})
  }

  const appFramework = path.join(
    app, 'Contents', 'Frameworks', path.basename(sourceFramework))
  const appVersion = await frameworkVersionDirectory(appFramework)
  const sourceResource = path.join(outDir, 'gen', 'repack', 'brave_resources.pak')
  const appResource = path.join(appVersion, 'Resources', 'brave_resources.pak')
  const sourceChromiumResource = path.join(outDir, 'gen', 'repack', 'resources.pak')
  const appChromiumResource = path.join(appVersion, 'Resources', 'resources.pak')
  const sourceLocaleDirectory = path.join(outDir, 'gen', 'repack', 'locales')
  const initialAppResources = await namedFilesUnder(app, 'brave_resources.pak')
  const initialAppChromiumResources = await namedFilesUnder(app, 'resources.pak')
  const appResourceTargets = [...new Set([...initialAppResources, appResource])]
  const appChromiumResourceTargets = [
    ...new Set([...initialAppChromiumResources, appChromiumResource]),
  ]
  const scaledPackSources = SCALED_RESOURCE_PACKS.map(name => ({
    name,
    source: path.join(outDir, 'gen', 'repack', name),
  }))
  const scaledPackTargets = (await Promise.all(scaledPackSources.map(async item => {
    const existing = await namedFilesUnder(app, item.name)
    const destinations = existing.length > 0
      ? existing
      : [path.join(appVersion, 'Resources', item.name)]
    return destinations.map(destination => ({...item, destination}))
  }))).flat()
  const sourceDylibs = await dylibsIn(outDir)
  const sourceLocalePacks = await localePacksIn(sourceLocaleDirectory)
  const localeTargets = sourceLocalePacks.map(source => {
    const locale = path.basename(source, '.pak')
    return {
      destination: path.join(appVersion, 'Resources', `${locale}.lproj`, 'locale.pak'),
      locale,
      source,
    }
  })
  const appDylibDir = path.join(app, 'Contents', 'Frameworks')

  if (sourceDylibs.length === 0 || !(await pathExists(sourceResource)) ||
      !(await pathExists(sourceChromiumResource)) || sourceLocalePacks.length === 0 ||
      !(await Promise.all(scaledPackSources.map(item =>
        pathExists(item.source)))).every(Boolean)) {
    throw new Error('Current source artifacts are incomplete')
  }

  const buildManifest = await verifyBuildManifest(braveRoot, outDir)
  const nativeSource = await latestSource(braveRoot, SOURCE_GROUPS.native)
  const resourceSource = await latestSource(braveRoot, SOURCE_GROUPS.braveResources)
  const chromiumResourceSource = await latestSource(
    braveRoot, SOURCE_GROUPS.chromiumResources)
  const localeSource = await latestSource(braveRoot, SOURCE_GROUPS.localeResources)
  const scaledResourceSource = await latestSource(
    braveRoot, SOURCE_GROUPS.scaledResources)
  const sourceLibchrome = path.join(outDir, 'libchrome_dll.dylib')
  const nativeArtifactStat = await fs.stat(sourceLibchrome)
  const resourceArtifactStat = await fs.stat(sourceResource)
  const chromiumResourceArtifactStat = await fs.stat(sourceChromiumResource)
  const localeArtifactStats = await Promise.all(sourceLocalePacks.map(async file => ({
    file,
    stat: await fs.stat(file),
  })))
  const scaledArtifactStats = await Promise.all(scaledPackSources.map(async item => ({
    file: item.source,
    stat: await fs.stat(item.source),
  })))
  const oldestLocaleArtifact = localeArtifactStats.reduce((oldest, current) =>
    current.stat.mtimeMs < oldest.stat.mtimeMs ? current : oldest)
  const oldestScaledArtifact = scaledArtifactStats.reduce((oldest, current) =>
    current.stat.mtimeMs < oldest.stat.mtimeMs ? current : oldest)
  const freshness = {
    native: {
      artifact: sourceLibchrome,
      artifactMtime: nativeArtifactStat.mtime.toISOString(),
      latestSource: nativeSource.file,
      latestSourceMtime: nativeSource.mtimeMs
        ? new Date(nativeSource.mtimeMs).toISOString()
        : null,
      pass: nativeSource.mtimeMs <= nativeArtifactStat.mtimeMs,
    },
    resources: {
      artifact: sourceResource,
      artifactMtime: resourceArtifactStat.mtime.toISOString(),
      latestSource: resourceSource.file,
      latestSourceMtime: resourceSource.mtimeMs
        ? new Date(resourceSource.mtimeMs).toISOString()
        : null,
      pass: resourceSource.mtimeMs <= resourceArtifactStat.mtimeMs,
    },
    chromiumResources: {
      artifact: sourceChromiumResource,
      artifactMtime: chromiumResourceArtifactStat.mtime.toISOString(),
      latestSource: chromiumResourceSource.file,
      latestSourceMtime: chromiumResourceSource.mtimeMs
        ? new Date(chromiumResourceSource.mtimeMs).toISOString()
        : null,
      pass: chromiumResourceSource.mtimeMs <= chromiumResourceArtifactStat.mtimeMs,
    },
    locales: {
      artifact: oldestLocaleArtifact.file,
      artifactCount: sourceLocalePacks.length,
      artifactMtime: oldestLocaleArtifact.stat.mtime.toISOString(),
      latestSource: localeSource.file,
      latestSourceMtime: localeSource.mtimeMs
        ? new Date(localeSource.mtimeMs).toISOString()
        : null,
      pass: localeSource.mtimeMs <= oldestLocaleArtifact.stat.mtimeMs,
    },
    scaledResources: {
      artifact: oldestScaledArtifact.file,
      artifactCount: scaledPackSources.length,
      artifactMtime: oldestScaledArtifact.stat.mtime.toISOString(),
      latestSource: scaledResourceSource.file,
      latestSourceMtime: scaledResourceSource.mtimeMs
        ? new Date(scaledResourceSource.mtimeMs).toISOString()
        : null,
      pass: scaledResourceSource.mtimeMs <= oldestScaledArtifact.stat.mtimeMs,
    },
  }
  if (!freshness.native.pass || !freshness.resources.pass ||
      !freshness.chromiumResources.pass || !freshness.locales.pass ||
      !freshness.scaledResources.pass) {
    throw Object.assign(new Error('Source artifacts are older than relevant source edits'), {
      details: freshness,
    })
  }

  const copied = []
  if (prepareApp) {
    const initialAppDylibs = await dylibsIn(appDylibDir)
    const initialAppNames = new Map(
      initialAppDylibs.map(file => [path.basename(file), file]))
    const [sourceUuids, initialAppUuids, sourceLoads, initialAppLoads,
      sourceContentHashes, initialAppContentHashes] = await Promise.all([
      uuidMap(sourceDylibs),
      uuidMap(initialAppDylibs),
      loadCommandMap(sourceDylibs),
      loadCommandMap(initialAppDylibs),
      unsignedMachOHashMap(sourceDylibs),
      unsignedMachOHashMap(initialAppDylibs),
    ])
    for (const source of sourceDylibs) {
      const destination = path.join(appDylibDir, path.basename(source))
      const current = initialAppNames.get(path.basename(source))
      const sourceUuid = sourceUuids.get(path.resolve(source)) || []
      const appUuid = current
        ? initialAppUuids.get(path.resolve(current)) || []
        : []
      const sourceLoad = sourceLoads.get(path.resolve(source)) || []
      const appLoad = current
        ? initialAppLoads.get(path.resolve(current)) || []
        : []
      const sourceContentHash = sourceContentHashes.get(path.resolve(source))
      const appContentHash = current
        ? initialAppContentHashes.get(path.resolve(current))
        : null
      if (!current || JSON.stringify(sourceUuid) !== JSON.stringify(appUuid) ||
          JSON.stringify(sourceLoad) !== JSON.stringify(appLoad) ||
          sourceContentHash !== appContentHash) {
        await fs.copyFile(source, destination)
        copied.push(destination)
      }
    }
    for (const destination of appResourceTargets) {
      if (await copyIfDifferent(sourceResource, destination)) {
        copied.push(destination)
      }
    }
    for (const destination of appChromiumResourceTargets) {
      if (await copyIfDifferent(sourceChromiumResource, destination)) {
        copied.push(destination)
      }
    }
    for (const {destination, source} of scaledPackTargets) {
      await fs.mkdir(path.dirname(destination), {recursive: true})
      if (await copyIfDifferent(source, destination)) {
        copied.push(destination)
      }
    }
    for (const {destination, source} of localeTargets) {
      await fs.mkdir(path.dirname(destination), {recursive: true})
      if (await copyIfDifferent(source, destination)) {
        copied.push(destination)
      }
    }
    if (copied.length > 0) {
      await log(`Copied ${copied.length} current artifacts into QA app`)
      await run('codesign', ['--force', '--deep', '--sign', '-', app], {check: true})
    }
  }

  const appDylibs = await dylibsIn(appDylibDir)
  const sourceNames = sourceDylibs.map(file => path.basename(file))
  const appNames = new Set(appDylibs.map(file => path.basename(file)))
  const missing = sourceNames.filter(name => !appNames.has(name))
  const extra = appDylibs
    .map(file => path.basename(file))
    .filter(name => !sourceNames.includes(name))
  if (missing.length > 0) {
    throw Object.assign(new Error(`QA app is missing ${missing.length} dylibs`), {
      details: {missing},
    })
  }
  if (extra.length > 0) {
    throw Object.assign(new Error(`QA app has ${extra.length} unexpected dylibs`), {
      details: {extra},
    })
  }

  const [sourceUuids, appUuids, sourceLoads, appLoads,
    sourceContentHashes, appContentHashes] = await Promise.all([
    uuidMap(sourceDylibs),
    uuidMap(appDylibs),
    loadCommandMap(sourceDylibs),
    loadCommandMap(appDylibs),
    unsignedMachOHashMap(sourceDylibs),
    unsignedMachOHashMap(appDylibs),
  ])
  const comparisons = sourceDylibs.map(source => {
    const destination = path.join(appDylibDir, path.basename(source))
    const sourceUuid = sourceUuids.get(path.resolve(source)) || []
    const appUuid = appUuids.get(path.resolve(destination)) || []
    const sourceLoadCommands = sourceLoads.get(path.resolve(source)) || []
    const appLoadCommands = appLoads.get(path.resolve(destination)) || []
    const sourceContentSha256 = sourceContentHashes.get(path.resolve(source))
    const appContentSha256 = appContentHashes.get(path.resolve(destination))
    return {
      app: destination,
      appContentSha256,
      appLoadCommands,
      appUuid,
      match: JSON.stringify(sourceUuid) === JSON.stringify(appUuid) &&
        JSON.stringify(sourceLoadCommands) === JSON.stringify(appLoadCommands) &&
        sourceContentSha256 === appContentSha256,
      name: path.basename(source),
      source,
      sourceContentSha256,
      sourceLoadCommands,
      sourceUuid,
    }
  })
  const mismatched = comparisons.filter(comparison => !comparison.match)
  if (mismatched.length > 0) {
    throw Object.assign(new Error(`QA app has ${mismatched.length} mismatched dylibs`), {
      details: {mismatched},
    })
  }

  const appResources = await namedFilesUnder(app, 'brave_resources.pak')
  const appChromiumResources = await namedFilesUnder(app, 'resources.pak')
  if (appResources.length === 0) {
    throw new Error('QA app has no brave_resources.pak files')
  }
  if (appChromiumResources.length === 0) {
    throw new Error('QA app has no resources.pak files')
  }
  const scaledPackComparisons = await Promise.all(scaledPackTargets.map(async item => {
    if (!(await pathExists(item.destination))) {
      return {...item, match: false, reason: 'missing'}
    }
    const [source, app] = await Promise.all([
      fileRecord(item.source),
      fileRecord(item.destination),
    ])
    return {
      app,
      match: source.sha256 === app.sha256,
      name: item.name,
      source,
    }
  }))
  const mismatchedScaledPacks = scaledPackComparisons.filter(
    comparison => !comparison.match)
  if (mismatchedScaledPacks.length > 0) {
    throw Object.assign(
      new Error(
        `${mismatchedScaledPacks.length} QA app scaled resource packs do not match current output`),
      {details: mismatchedScaledPacks})
  }
  const localeComparisons = await Promise.all(localeTargets.map(async item => {
    if (!(await pathExists(item.destination))) {
      return {...item, match: false, reason: 'missing'}
    }
    const [source, app] = await Promise.all([
      fileRecord(item.source),
      fileRecord(item.destination),
    ])
    return {
      app,
      locale: item.locale,
      match: source.sha256 === app.sha256,
      source,
    }
  }))
  const mismatchedLocales = localeComparisons.filter(comparison => !comparison.match)
  if (mismatchedLocales.length > 0) {
    throw Object.assign(
      new Error(`${mismatchedLocales.length} QA app locale packs do not match current output`),
      {details: mismatchedLocales})
  }
  const localeDigest = createHash('sha256')
  for (const comparison of localeComparisons) {
    localeDigest.update(`${comparison.locale}\0${comparison.source.sha256}\n`)
  }
  const [sourceResourceRecord, appResourceRecord, appResourceRecords,
    sourceChromiumResourceRecord, appChromiumResourceRecord,
    appChromiumResourceRecords, sourceLibRecord, appLibRecord] =
    await Promise.all([
      fileRecord(sourceResource),
      fileRecord(appResource),
      Promise.all(appResources.map(fileRecord)),
      fileRecord(sourceChromiumResource),
      fileRecord(appChromiumResource),
      Promise.all(appChromiumResources.map(fileRecord)),
      fileRecord(sourceLibchrome),
      fileRecord(path.join(appDylibDir, 'libchrome_dll.dylib')),
    ])
  const mismatchedResources = appResourceRecords.filter(record =>
    record.sha256 !== sourceResourceRecord.sha256)
  if (mismatchedResources.length > 0) {
    throw Object.assign(
      new Error(`${mismatchedResources.length} QA app resource packs do not match current output`),
      {details: mismatchedResources})
  }
  const mismatchedChromiumResources = appChromiumResourceRecords.filter(record =>
    record.sha256 !== sourceChromiumResourceRecord.sha256)
  if (mismatchedChromiumResources.length > 0) {
    throw Object.assign(
      new Error(`${mismatchedChromiumResources.length} QA app Chromium resource packs do not match current output`),
      {details: mismatchedChromiumResources})
  }
  const sourceLibContentSha256 = sourceContentHashes.get(path.resolve(sourceLibchrome))
  const appLibContentSha256 = appContentHashes.get(path.resolve(
    path.join(appDylibDir, 'libchrome_dll.dylib')))
  if (sourceLibContentSha256 !== appLibContentSha256) {
    throw new Error('QA app libchrome_dll.dylib content does not match current output')
  }

  const signature = await run(
    'codesign', ['--verify', '--deep', '--strict', '--verbose=2', app])
  if (signature.code !== 0) {
    throw new Error(`QA app signature invalid: ${signature.stderr}`)
  }

  return {
    app,
    baseApp: {
      app: appExecutables,
      refreshed: baseAppRefreshed,
      source: sourceAppExecutables,
    },
    buildManifest,
    copied,
    dylibs: {
      count: comparisons.length,
      extra,
      missing,
      mismatched,
    },
    freshness,
    libchrome: {
      app: appLibRecord,
      appContentSha256: appLibContentSha256,
      appUuid: appUuids.get(path.resolve(path.join(appDylibDir, 'libchrome_dll.dylib'))),
      source: sourceLibRecord,
      sourceContentSha256: sourceLibContentSha256,
      sourceUuid: sourceUuids.get(path.resolve(sourceLibchrome)),
    },
    resources: {
      allApp: appResourceRecords,
      app: appResourceRecord,
      chromiumAllApp: appChromiumResourceRecords,
      chromiumApp: appChromiumResourceRecord,
      chromiumSource: sourceChromiumResourceRecord,
      locales: {
        count: localeComparisons.length,
        digest: localeDigest.digest('hex'),
        packs: localeComparisons,
      },
      scaled: {
        count: scaledPackComparisons.length,
        packs: scaledPackComparisons,
      },
      source: sourceResourceRecord,
    },
    signature: {pass: true, output: signature.stderr.trim()},
  }
}
