import fs from 'node:fs/promises'
import path from 'node:path'

import {pathExists, run} from '../lib/system.mjs'

const COMPONENT_UNIT_FILTER = [
  'PersonaGeneratorTest.*',
  'PersonaSerializationTest.*',
  'PersonaServiceTest.*',
  'ProfileProxyConfigTest.*',
  'ProxyGeoProviderTest.*',
].join(':')

const BRAVE_UNIT_FILTER = [
  'BraveColorMixersTest.*',
  'ProfileProxyConfigServiceTest.*',
].join(':')

const NET_FILTER = [
  '*ConfiguredProxyAuthCredentialsRetryNoTunnel*',
  '*RejectedConfiguredProxyAuthCredentialsFailNoTunnel*',
].join(':')

const PERFORMANCE_MANAGER_FILTER =
  'WorkerWatcherTest.ServiceWorkerStopsAfterSharedWorkerClientDies'

const LEGACY_PERSONA_CONFLICT_FILTERS = [
  'EphemeralStorage1pBrowserTest.FarblingTokenIsEphemeral',
  'BraveDeviceMemoryFarblingBrowserTest.FarbleDeviceMemory',
  'BraveFarblingBrowserTest.FarblingTokenBehaviourAfterRestart/*Enabled',
  'BraveFarblingBrowserTest.FarblingTokenIsClearedAfterWebsiteClear/*',
  'BraveFarblingBrowserTest.NavigatorPluginsAreFarbled/*',
  'BraveNavigatorHardwareConcurrencyFarblingBrowserTest.FarbleNavigatorHardwareConcurrency',
  'BraveNavigatorHardwareConcurrencyFarblingBrowserTest.FarbleNavigatorHardwareConcurrencyWorkers',
  'BraveNavigatorLanguagesFarblingBrowserTest.FarbleLanguages',
  'BraveNavigatorPluginsFarblingBrowserTest.FarbleNavigatorPlugins',
  'BraveNavigatorPluginsFarblingBrowserTest.FarbleNavigatorPluginsBuiltin',
  'BraveNavigatorPluginsFarblingBrowserTest.FarbleNavigatorPluginsReset',
  'BraveOffscreenCanvasFarblingBrowserTest.FarbleGetImageData',
  'BraveScreenFarblingBrowserTest_EnableFlag.*',
  'BraveWebGLExtensionFarblingTest.FarbleVendorAndRendererDebugInfoWebGL/*',
  'BraveWebGLFarblingBrowserTest.FarbleGetParameterWebGL2',
]

const PERSONA_BROWSER_FILTER = `${[
  'FingerprintBrowserProfileProxyBrowserTest.*',
  'BraveSettingsUIBrowserTest.*',
  '*Farbling*',
].join(':')}-${LEGACY_PERSONA_CONFLICT_FILTERS.join(':')}`

const LEGACY_FARBLING_FILTER = LEGACY_PERSONA_CONFLICT_FILTERS.join(':')

const TEST_SOURCE_GROUPS = {
  braveComponents: [
    'components/fingerprint_browser/browser',
    'third_party/libmaxminddb/include/maxminddb_config.h',
  ],
  braveUnit: [
    'browser/ui/color',
    'browser/net/proxy_resolution',
    'components/fingerprint_browser/browser',
  ],
  browser: [
    'browser/brave_content_browser_client.cc',
    'browser/brave_content_browser_client.h',
    'browser/farbling',
    'browser/fingerprint_browser',
    'browser/ui/webui',
    'chromium_src/chrome/browser/content_settings',
    'chromium_src/chrome/renderer/worker_content_settings_client.cc',
    'chromium_src/components/content_settings',
    'chromium_src/content',
    'chromium_src/third_party/blink',
    'components/content_settings/renderer',
    'components/fingerprint_browser',
    'third_party/blink/renderer/core/farbling',
  ],
  net: [
    '../net/http/http_network_transaction.cc',
    '../net/http/http_network_transaction.h',
    '../net/http/http_network_transaction_unittest.cc',
    'patches/net-http-http_network_transaction.cc.patch',
    'patches/net-http-http_network_transaction.h.patch',
    'patches/net-http-http_network_transaction_unittest.cc.patch',
  ],
  performanceManager: [
    '../components/performance_manager/worker_watcher.cc',
    '../components/performance_manager/worker_watcher_unittest.cc',
    'patches/components-performance_manager-worker_watcher.cc.patch',
    'patches/components-performance_manager-worker_watcher_unittest.cc.patch',
  ],
}

async function latestFile(target) {
  if (!(await pathExists(target))) return null
  const stat = await fs.stat(target)
  if (stat.isFile()) return {file: target, mtimeMs: stat.mtimeMs}
  let latest = null
  for (const entry of await fs.readdir(target, {withFileTypes: true})) {
    const candidate = await latestFile(path.join(target, entry.name))
    if (candidate && (!latest || candidate.mtimeMs > latest.mtimeMs)) {
      latest = candidate
    }
  }
  return latest
}

async function latestTestSource(braveRoot, paths) {
  let latest = null
  for (const relative of paths) {
    const candidate = await latestFile(path.resolve(braveRoot, relative))
    if (candidate && (!latest || candidate.mtimeMs > latest.mtimeMs)) {
      latest = candidate
    }
  }
  return latest || {file: null, mtimeMs: 0}
}

export async function runTestBinary({
  binary,
  extraArgs = [],
  filter,
  logFile,
  source,
  timeoutMs,
}) {
  if (!(await pathExists(binary))) {
    throw new Error(`Required test binary missing: ${binary}`)
  }
  const binaryStat = await fs.stat(binary)
  if (source?.mtimeMs && binaryStat.mtimeMs <= source.mtimeMs) {
    throw Object.assign(new Error(`Required test binary is stale: ${binary}`), {
      details: {binaryMtimeMs: binaryStat.mtimeMs, source},
    })
  }
  const result = await run(binary, [
    `--gtest_filter=${filter}`,
    '--test-launcher-bot-mode',
    '--test-launcher-jobs=1',
    '--test-launcher-retry-limit=0',
    ...extraArgs,
  ], {timeoutMs})
  const output = `${result.stdout}\n${result.stderr}`
  await fs.writeFile(logFile, output)
  if (result.timedOut || result.code !== 0) {
    throw new Error(result.timedOut
      ? `${path.basename(binary)} timed out after ${timeoutMs}ms`
      : `${path.basename(binary)} exited ${result.code}`)
  }
  if (/No matching tests to run/i.test(output)) {
    throw new Error(`${path.basename(binary)} filter matched no tests: ${filter}`)
  }
  return {
    binary,
    binaryMtime: binaryStat.mtime.toISOString(),
    durationMs: result.durationMs,
    extraArgs,
    filter,
    latestSource: source,
    logFile,
  }
}

export async function runUnitTests({braveRoot, dirs, outDir}) {
  const [componentSource, braveUnitSource] = await Promise.all([
    latestTestSource(braveRoot, TEST_SOURCE_GROUPS.braveComponents),
    latestTestSource(braveRoot, TEST_SOURCE_GROUPS.braveUnit),
  ])
  return await Promise.all([
    runTestBinary({
      binary: path.join(outDir, 'brave_components_unittests'),
      filter: COMPONENT_UNIT_FILTER,
      logFile: path.join(dirs.logs, 'brave_components_unittests.log'),
      source: componentSource,
      timeoutMs: 20 * 60 * 1000,
    }),
    runTestBinary({
      binary: path.join(outDir, 'brave_unit_tests'),
      filter: BRAVE_UNIT_FILTER,
      logFile: path.join(dirs.logs, 'brave_unit_tests.log'),
      source: braveUnitSource,
      timeoutMs: 20 * 60 * 1000,
    }),
  ])
}

export async function runNetTests({braveRoot, dirs, outDir}) {
  return await runTestBinary({
    binary: path.join(outDir, 'net_unittests'),
    filter: NET_FILTER,
    logFile: path.join(dirs.logs, 'net_unittests.log'),
    source: await latestTestSource(braveRoot, TEST_SOURCE_GROUPS.net),
    timeoutMs: 20 * 60 * 1000,
  })
}

export async function runPerformanceManagerTests({braveRoot, dirs, outDir}) {
  return await runTestBinary({
    binary: path.join(outDir, 'fingerprint_browser_worker_watcher_unittests'),
    filter: PERFORMANCE_MANAGER_FILTER,
    logFile: path.join(dirs.logs, 'performance_manager_unit_tests.log'),
    source: await latestTestSource(
      braveRoot, TEST_SOURCE_GROUPS.performanceManager),
    timeoutMs: 20 * 60 * 1000,
  })
}

export async function runBrowserTests({braveRoot, dirs, outDir}) {
  const binary = path.join(outDir, 'brave_browser_tests')
  const source = await latestTestSource(braveRoot, TEST_SOURCE_GROUPS.browser)
  const legacy = await runTestBinary({
    binary,
    extraArgs: ['--disable-fingerprint-browser-persona-for-testing'],
    filter: LEGACY_FARBLING_FILTER,
    logFile: path.join(dirs.logs, 'brave_browser_tests_legacy_farbling.log'),
    source,
    timeoutMs: 60 * 60 * 1000,
  })
  const persona = await runTestBinary({
    binary,
    filter: PERSONA_BROWSER_FILTER,
    logFile: path.join(dirs.logs, 'brave_browser_tests_persona.log'),
    source,
    timeoutMs: 60 * 60 * 1000,
  })
  return [legacy, persona]
}
