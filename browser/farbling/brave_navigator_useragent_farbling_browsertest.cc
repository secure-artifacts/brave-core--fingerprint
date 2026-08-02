/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include "base/functional/function_ref.h"
#include "base/json/json_reader.h"
#include "base/memory/scoped_refptr.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/test/thread_test_helper.h"
#include "base/version.h"
#include "brave/browser/brave_content_browser_client.h"
#include "brave/browser/extensions/brave_base_local_data_files_browsertest.h"
#include "brave/browser/fingerprint_browser/persona_service_factory.h"
#include "brave/components/brave_component_updater/browser/local_data_files_service.h"
#include "brave/components/brave_shields/core/browser/brave_shields_utils.h"
#include "brave/components/brave_shields/core/common/features.h"
#include "brave/components/constants/brave_paths.h"
#include "brave/components/constants/pref_names.h"
#include "brave/components/fingerprint_browser/browser/persona.h"
#include "brave/components/fingerprint_browser/browser/persona_service.h"
#include "brave/components/fingerprint_browser/browser/pref_names.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/net/system_network_context_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/embedder_support/user_agent_utils.h"
#include "components/permissions/permission_request.h"
#include "components/prefs/pref_service.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_mock_cert_verifier.h"
#include "content/public/test/simple_url_loader_test_helper.h"
#include "extensions/buildflags/buildflags.h"
#include "media/base/media_switches.h"
#include "net/dns/mock_host_resolver.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_status_code.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "net/test/embedded_test_server/install_default_websocket_handlers.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "services/network/public/cpp/network_switches.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "third_party/abseil-cpp/absl/strings/str_format.h"

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "chrome/browser/extensions/chrome_test_extension_loader.h"
#include "extensions/browser/api/offscreen/offscreen_document_manager.h"
#include "extensions/browser/background_script_executor.h"
#include "extensions/browser/lazy_context_id.h"
#include "extensions/browser/lazy_context_task_queue.h"
#include "extensions/browser/offscreen_document_host.h"
#include "extensions/common/extension.h"
#include "extensions/common/switches.h"
#include "extensions/test/test_extension_dir.h"
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)

using brave_shields::ControlType;
using content::TitleWatcher;

namespace {

constexpr char kUserAgentScript[] = "navigator.userAgent";

constexpr char kBrandScript[] =
    "navigator.userAgentData.brands[0].brand + '|' + "
    "navigator.userAgentData.brands[1].brand + '|' + "
    "navigator.userAgentData.brands[2].brand";

constexpr char kGetHighEntropyValuesScript[] = R"(
  navigator.userAgentData.getHighEntropyValues(
      ["fullVersionList", "uaFullVersion"]).then(
          (values) => {return values;})
)";

constexpr char kPersonaGetHighEntropyValuesScript[] = R"(
  navigator.userAgentData.getHighEntropyValues([
    "architecture",
    "bitness",
    "fullVersionList",
    "platform",
    "platformVersion",
    "uaFullVersion"
  ]).then((values) => values)
)";

constexpr char kPersonaCanvasFingerprintScript[] = R"(
  (async () => {
    const hashBytes = (bytes) => {
      let hash = 0;
      for (let i = 0; i < bytes.length; ++i) {
        hash = (hash + bytes[i] * ((i % 8191) + 1)) % 1000000007;
      }
      return hash;
    };
    const hashString = (value) => {
      let hash = 0;
      for (let i = 0; i < value.length; ++i) {
        hash = (hash + value.charCodeAt(i) * ((i % 8191) + 1)) % 1000000007;
      }
      return hash;
    };
    const canvas = document.createElement('canvas');
    canvas.width = 32;
    canvas.height = 32;
    const ctx = canvas.getContext('2d');
    ctx.fillStyle = 'rgb(255, 0, 0)';
    ctx.fillRect(0, 0, canvas.width, canvas.height);

    const imageDataHash1 =
        hashBytes(ctx.getImageData(0, 0, canvas.width, canvas.height).data);
    const imageDataHash2 =
        hashBytes(ctx.getImageData(0, 0, canvas.width, canvas.height).data);
    const dataUrl1 = canvas.toDataURL('image/png');
    const dataUrl2 = canvas.toDataURL('image/png');
    const blob1 =
        await new Promise((resolve) => canvas.toBlob(resolve, 'image/png'));
    const blob2 =
        await new Promise((resolve) => canvas.toBlob(resolve, 'image/png'));
    const blobHash1 = blob1
        ? hashBytes(new Uint8Array(await blob1.arrayBuffer()))
        : -1;
    const blobHash2 = blob2
        ? hashBytes(new Uint8Array(await blob2.arrayBuffer()))
        : -1;
    return {
      imageDataHash: imageDataHash1,
      imageDataStable: imageDataHash1 === imageDataHash2,
      dataUrlHash: hashString(dataUrl1),
      dataUrlStable: dataUrl1 === dataUrl2,
      blobHash: blobHash1,
      blobStable: blobHash1 === blobHash2
    };
  })()
)";

constexpr char kPersonaWorkerPagePath[] = "/persona-workers.html";
constexpr char kSimplePagePath[] = "/simple.html";
constexpr char kPersonaDedicatedWorkerPath[] = "/persona-dedicated-worker.js";
constexpr char kPersonaNestedWorkerPath[] = "/persona-nested-worker.js";
constexpr char kPersonaSharedWorkerPath[] = "/persona-shared-worker.js";
constexpr char kPersonaServiceWorkerPath[] = "/persona-service-worker.js";
constexpr char kWorkersUserAgentPagePath[] =
    "/navigator/workers-useragent.html";
constexpr char kWorkersUserAgentScriptPath[] =
    "/navigator/workers-useragent.js";
constexpr char kWorkersUserAgentNetworkPagePath[] =
    "/navigator/workers-useragent-network.html";
constexpr char kWorkersUserAgentNetworkScriptPath[] =
    "/navigator/workers-useragent-network.js";
constexpr char kServiceWorkersUserAgentPagePath[] =
    "/navigator/service-workers-useragent.html";
constexpr char kServiceWorkersUserAgentScriptPath[] =
    "/navigator/service-workers-useragent.js";
constexpr char kSharedWorkersUserAgentPagePath[] =
    "/navigator/shared-workers-useragent.html";
constexpr char kSharedWorkersWorkerScriptPath[] =
    "/navigator/shared-workers-worker.js";

constexpr char kPersonaWorkerFingerprintScript[] = R"(
  const fingerprint = () => {
    const hash = (bytes) => {
      let value = 0;
      for (let i = 0; i < bytes.length; ++i) {
        value = (value + bytes[i] * ((i % 8191) + 1)) % 1000000007;
      }
      return value;
    };
    const canvas = new OffscreenCanvas(32, 32);
    const context = canvas.getContext('2d');
    context.fillStyle = 'rgb(255, 0, 0)';
    context.fillRect(0, 0, canvas.width, canvas.height);
    const webglCanvas = new OffscreenCanvas(1, 1);
    const gl = webglCanvas.getContext('webgl');
    const debug = gl?.getExtension('WEBGL_debug_renderer_info');
    return {
      userAgent: navigator.userAgent,
      canvas: hash(context.getImageData(0, 0, canvas.width, canvas.height).data),
      renderer: debug ? gl.getParameter(debug.UNMASKED_RENDERER_WEBGL) : '',
      audioApiExposed: typeof AudioContext !== 'undefined' ||
          typeof OfflineAudioContext !== 'undefined'
    };
  };
)";

std::unique_ptr<net::test_server::HttpResponse> HandlePersonaWorkerRequest(
    const net::test_server::HttpRequest& request) {
  const std::string path(request.GetURL().path());
  std::string content;
  const char* content_type = "application/javascript";

  if (path == kPersonaWorkerPagePath || path == kSimplePagePath) {
    content = "<!doctype html><title>Persona Workers</title>";
    content_type = "text/html";
  } else if (path == kWorkersUserAgentPagePath ||
             path == kWorkersUserAgentNetworkPagePath) {
    const char* worker_path = path == kWorkersUserAgentPagePath
                                  ? kWorkersUserAgentScriptPath
                                  : kWorkersUserAgentNetworkScriptPath;
    content =
        base::StrCat({"<!doctype html><script>const worker = new "
                      "Worker('",
                      worker_path,
                      "'); worker.onmessage = () => document.title = "
                      "'pass'; worker.postMessage('ready');</script>"});
    content_type = "text/html";
  } else if (path == kServiceWorkersUserAgentPagePath) {
    content = R"(
      <!doctype html><script>
      navigator.serviceWorker.register('/navigator/service-workers-useragent.js')
          .then(() => document.title = 'pass');
      </script>
    )";
    content_type = "text/html";
  } else if (path == kSharedWorkersUserAgentPagePath) {
    content = R"(
      <!doctype html><script>
      const worker = new SharedWorker('/navigator/shared-workers-worker.js');
      worker.port.onmessage = () => document.title = 'pass';
      worker.port.start();
      worker.port.postMessage('ready');
      </script>
    )";
    content_type = "text/html";
  } else if (path == kWorkersUserAgentScriptPath ||
             path == kWorkersUserAgentNetworkScriptPath) {
    content = "self.onmessage = () => self.postMessage(navigator.userAgent);";
  } else if (path == kServiceWorkersUserAgentScriptPath) {
    content = R"(
      self.addEventListener('install', () => self.skipWaiting());
      self.addEventListener('activate', (event) => {
        event.waitUntil(self.clients.claim());
      });
    )";
  } else if (path == kSharedWorkersWorkerScriptPath) {
    content = R"(
      self.onconnect = (event) => {
        const port = event.ports[0];
        port.onmessage = () => port.postMessage(navigator.userAgent);
        port.start();
      };
    )";
  } else if (path == kPersonaDedicatedWorkerPath) {
    content = base::StrCat({R"(
      )",
                            kPersonaWorkerFingerprintScript, R"(
      self.onmessage = () => {
        const nested = new Worker('/persona-nested-worker.js');
        nested.onmessage = (event) => {
          self.postMessage({worker: fingerprint(), nested: event.data});
          nested.terminate();
        };
        nested.postMessage('fingerprint');
      };
    )"});
  } else if (path == kPersonaNestedWorkerPath) {
    content = base::StrCat({R"(
      )",
                            kPersonaWorkerFingerprintScript, R"(
      self.onmessage = () => self.postMessage(fingerprint());
    )"});
  } else if (path == kPersonaSharedWorkerPath) {
    content = base::StrCat({R"(
      )",
                            kPersonaWorkerFingerprintScript, R"(
      self.onconnect = (event) => {
        const port = event.ports[0];
        port.onmessage = () => port.postMessage(fingerprint());
        port.start();
      };
    )"});
  } else if (path == kPersonaServiceWorkerPath) {
    content = base::StrCat({R"(
      )",
                            kPersonaWorkerFingerprintScript, R"(
      self.addEventListener('install', () => self.skipWaiting());
      self.addEventListener('activate', (event) => {
        event.waitUntil(self.clients.claim());
      });
      self.addEventListener('message', (event) => {
        const port = event.ports[0];
        if (port) {
          port.postMessage(fingerprint());
        }
      });
    )"});
  } else {
    return nullptr;
  }

  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_code(net::HTTP_OK);
  response->set_content_type(content_type);
  response->set_content(content);
  return response;
}

bool HasBrandVersion(const base::ListValue& versions_list,
                     const fingerprint_browser::UserAgentBrand& expected) {
  for (const auto& brand_version : versions_list) {
    const std::string* brand = brand_version.GetDict().FindString("brand");
    const std::string* version = brand_version.GetDict().FindString("version");
    if (brand && version && *brand == expected.brand &&
        *version == expected.version) {
      return true;
    }
  }
  return false;
}

void CheckPersonaUserAgentMetadataVersionsList(
    const base::ListValue& versions_list,
    const std::vector<fingerprint_browser::UserAgentBrand>& expected_brands) {
  EXPECT_EQ(expected_brands.size(), versions_list.size());
  for (const auto& expected : expected_brands) {
    EXPECT_TRUE(HasBrandVersion(versions_list, expected))
        << expected.brand << " " << expected.version;
  }
}

void ExpectDictInt(const base::DictValue& values,
                   std::string_view key,
                   int expected) {
  const std::optional<int> value = values.FindInt(key);
  ASSERT_TRUE(value.has_value()) << key;
  EXPECT_EQ(expected, *value) << key;
}

void ExpectDictDouble(const base::DictValue& values,
                      std::string_view key,
                      double expected) {
  const std::optional<double> value = values.FindDouble(key);
  ASSERT_TRUE(value.has_value()) << key;
  EXPECT_DOUBLE_EQ(expected, *value) << key;
}

void ExpectDictBool(const base::DictValue& values,
                    std::string_view key,
                    bool expected) {
  const std::optional<bool> value = values.FindBool(key);
  ASSERT_TRUE(value.has_value()) << key;
  EXPECT_EQ(expected, *value) << key;
}

void ExpectDictString(const base::DictValue& values,
                      std::string_view key,
                      const std::string& expected) {
  const std::string* value = values.FindString(key);
  ASSERT_NE(nullptr, value) << key;
  EXPECT_EQ(expected, *value) << key;
}

void CheckUserAgentMetadataVersionsList(
    const base::ListValue& versions_list,
    const std::string& expected_version,
    base::FunctionRef<void(const std::string&)> check_greased_version) {
  // Expect 3 items in the list: Brave, Chromium, and greased.
  EXPECT_EQ(3UL, versions_list.size());

  bool has_brave_brand = false;
  bool has_chromium_brand = false;
  for (auto& brand_version : versions_list) {
    const std::string* brand = brand_version.GetDict().FindString("brand");
    ASSERT_NE(nullptr, brand);
    const std::string* version = brand_version.GetDict().FindString("version");
    ASSERT_NE(nullptr, version);
    if (*brand == "Brave") {
      has_brave_brand = true;
      EXPECT_EQ(expected_version, *version);
    } else if (*brand == "Chromium") {
      has_chromium_brand = true;
      EXPECT_EQ(expected_version, *version);
    } else {
      check_greased_version(*version);
    }
  }
  EXPECT_TRUE(has_brave_brand);
  EXPECT_TRUE(has_chromium_brand);
}

#if BUILDFLAG(ENABLE_EXTENSIONS)
// Wakes up the service worker for the `extension` in the given `profile`.
void WakeUpServiceWorker(const extensions::Extension& extension,
                         Profile& profile) {
  base::RunLoop run_loop;
  const auto context_id =
      extensions::LazyContextId::ForExtension(&profile, &extension);
  ASSERT_TRUE(context_id.IsForServiceWorker());
  context_id.GetTaskQueue()->AddPendingTask(
      context_id,
      base::BindOnce(
          [](std::unique_ptr<extensions::LazyContextTaskQueue::ContextInfo>) {})
          .Then(run_loop.QuitWhenIdleClosure()));
  run_loop.Run();
}

// Creates a new offscreen document through an API call, expecting success.
void ProgrammaticallyCreateOffscreenDocument(
    const extensions::Extension& extension,
    Profile& profile) {
  static constexpr char kScript[] =
      R"((async () => {
            let message;
            try {
              await chrome.offscreen.createDocument(
                  {
                    url: 'offscreen.html',
                    reasons: ['TESTING'],
                    justification: 'testing'
                  });
              message = 'success';
            } catch (e) {
              message = 'Error: ' + e.toString();
            }
            chrome.test.sendScriptResult(message);
          })();)";
  base::Value result = extensions::BackgroundScriptExecutor::ExecuteScript(
      &profile, extension.id(), kScript,
      extensions::BackgroundScriptExecutor::ResultCapture::kSendScriptResult);
  ASSERT_TRUE(result.is_string());
  EXPECT_EQ("success", result.GetString());
}
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)

}  // namespace

class BraveNavigatorUserAgentFarblingBrowserTest : public InProcessBrowserTest {
 public:
  BraveNavigatorUserAgentFarblingBrowserTest() {
    feature_list_.InitAndEnableFeature(
        brave_shields::features::kBraveShowStrictFingerprintingMode);
  }

  void SetUp() override {
    https_server_ = std::make_unique<net::EmbeddedTestServer>(
        net::test_server::EmbeddedTestServer::TYPE_HTTPS);
    https_server_->RegisterRequestHandler(
        base::BindRepeating(&HandlePersonaWorkerRequest));
    ASSERT_TRUE(https_server_->InitializeAndListen());
    InProcessBrowserTest::SetUp();
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();

    mock_cert_verifier_.mock_cert_verifier()->set_default_result(net::OK);
    host_resolver()->AddRule("*", "127.0.0.1");
    content::SetupCrossSiteRedirector(https_server_.get());

    base::FilePath test_data_dir;
    base::PathService::Get(brave::DIR_TEST_DATA, &test_data_dir);
    https_server_->ServeFilesFromDirectory(test_data_dir);
    net::test_server::InstallDefaultWebSocketHandlers(https_server_.get());
    https_server_->RegisterRequestMonitor(base::BindRepeating(
        &BraveNavigatorUserAgentFarblingBrowserTest::MonitorHTTPRequest,
        base::Unretained(this)));
    user_agents_.clear();

    https_server_->StartAcceptingConnections();
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    mock_cert_verifier_.SetUpCommandLine(command_line);
    command_line->AppendSwitchASCII(
        network::switches::kHostResolverRules,
        absl::StrFormat("MAP *:443 127.0.0.1:%d", https_server_->port()));
    command_line->AppendSwitchASCII(switches::kEnableBlinkFeatures,
                                    "FontAccess");
    command_line->AppendSwitch(switches::kUseFakeDeviceForMediaStream);
    command_line->AppendSwitch(switches::kUseFakeUIForMediaStream);
#if BUILDFLAG(ENABLE_EXTENSIONS)
    command_line->AppendSwitch(extensions::switches::kOffscreenDocumentTesting);
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)
  }

  void SetUpInProcessBrowserTestFixture() override {
    InProcessBrowserTest::SetUpInProcessBrowserTestFixture();
    mock_cert_verifier_.SetUpInProcessBrowserTestFixture();
  }

  void TearDownInProcessBrowserTestFixture() override {
    InProcessBrowserTest::TearDownInProcessBrowserTestFixture();
    mock_cert_verifier_.TearDownInProcessBrowserTestFixture();
  }

  void MonitorHTTPRequest(const net::test_server::HttpRequest& request) {
    request_paths_.push_back(request.relative_url);
    request_headers_.push_back(request.headers);
    auto user_agent = request.headers.find("user-agent");
    if (user_agent != request.headers.end()) {
      user_agents_.push_back(user_agent->second);
    }
  }

  net::EmbeddedTestServer* https_server() { return https_server_.get(); }

  std::string last_requested_http_user_agent() {
    if (user_agents_.empty()) {
      return "";
    }
    return user_agents_[user_agents_.size() - 1];
  }

  std::optional<std::string> last_requested_header(std::string_view name) {
    if (request_headers_.empty()) {
      return std::nullopt;
    }
    auto header = request_headers_.back().find(name);
    if (header == request_headers_.back().end()) {
      return std::nullopt;
    }
    return header->second;
  }

  std::optional<std::string> last_requested_header_for_path(
      std::string_view path,
      std::string_view name) {
    for (size_t i = request_headers_.size(); i > 0; --i) {
      if (request_paths_[i - 1] != path) {
        continue;
      }
      auto header = request_headers_[i - 1].find(name);
      if (header != request_headers_[i - 1].end()) {
        return header->second;
      }
    }
    return std::nullopt;
  }

  void ClearObservedRequests() {
    user_agents_.clear();
    request_headers_.clear();
    request_paths_.clear();
  }

  HostContentSettingsMap* content_settings() {
    return HostContentSettingsMapFactory::GetForProfile(browser()->profile());
  }

  void AllowFingerprinting(std::string domain) {
    brave_shields::SetFingerprintingControlType(
        content_settings(), ControlType::ALLOW,
        https_server()->GetURL(domain, "/"));
  }

  void BlockFingerprinting(std::string domain) {
    brave_shields::SetFingerprintingControlType(
        content_settings(), ControlType::BLOCK,
        https_server()->GetURL(domain, "/"));
  }

  void SetFingerprintingDefault(std::string domain) {
    brave_shields::SetFingerprintingControlType(
        content_settings(), ControlType::DEFAULT,
        https_server()->GetURL(domain, "/"));
  }

  void AllowUserAgentWebcompat(std::string domain) {
    brave_shields::SetWebcompatEnabled(
        content_settings(), ContentSettingsType::BRAVE_WEBCOMPAT_USER_AGENT,
        true, https_server()->GetURL(domain, "/"),
        g_browser_process->local_state());
  }

  content::WebContents* contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  void LoadURL(std::string_view host,
               scoped_refptr<network::SharedURLLoaderFactory> factory) {
    auto request = std::make_unique<network::ResourceRequest>();
    request->url = https_server_->GetURL(host, "/");
    content::SimpleURLLoaderTestHelper simple_loader_helper;
    auto simple_loader = network::SimpleURLLoader::Create(
        std::move(request), TRAFFIC_ANNOTATION_FOR_TESTS);

    simple_loader->DownloadToStringOfUnboundedSizeUntilCrashAndDie(
        factory.get(), simple_loader_helper.GetCallback());
    simple_loader_helper.WaitForCallback();
  }

  std::string LoadWithXmlHttpRequest(std::string_view path) {
    return EvalJs(contents(), content::JsReplace(R"(
      new Promise((resolve, reject) => {
        const xhr = new XMLHttpRequest();
        xhr.onload = () => resolve('done');
        xhr.onerror = () => reject('error');
        xhr.open('GET', $1);
        xhr.send();
      })
    )",
                                                 std::string(path)))
        .ExtractString();
  }

  void ExpectLastHeaderForPathEquals(std::string_view path,
                                     std::string_view name,
                                     std::string_view expected) {
    auto header = last_requested_header_for_path(path, name);
    ASSERT_TRUE(header);
    EXPECT_EQ(std::string(expected), *header);
  }

  content::EvalJsResult GetWebSocketRequestHeaders(const GURL& url) {
    return EvalJs(contents(), content::JsReplace(R"(
      new Promise((resolve, reject) => {
        const socket = new WebSocket($1);
        socket.onmessage = event => {
          socket.close();
          resolve(JSON.parse(event.data));
        };
        socket.onerror = () => reject('websocket error');
      })
    )",
                                                 url.spec()));
  }

#if BUILDFLAG(ENABLE_EXTENSIONS)
  void TestExtensionOffscreenDocument(std::string_view page_path,
                                      std::string_view script_path,
                                      std::string_view script_path2 = {}) {
    extensions::TestExtensionDir test_extension_dir;
    test_extension_dir.WriteManifest(R"({
      "name": "Offscreen Document Test",
      "manifest_version": 3,
      "version": "0.1",
      "background": {"service_worker": "background.js"},
      "permissions": ["offscreen"]
    })");
    test_extension_dir.WriteFile(FILE_PATH_LITERAL("background.js"), "");
    test_extension_dir.CopyFileTo(
        base::PathService::CheckedGet(brave::DIR_TEST_DATA)
            .AppendASCII(page_path),
        FILE_PATH_LITERAL("offscreen.html"));
    test_extension_dir.CopyFileTo(
        base::PathService::CheckedGet(brave::DIR_TEST_DATA)
            .AppendASCII(script_path),
        base::FilePath::FromASCII(script_path).BaseName().value());
    if (!script_path2.empty()) {
      test_extension_dir.CopyFileTo(
          base::PathService::CheckedGet(brave::DIR_TEST_DATA)
              .AppendASCII(script_path2),
          base::FilePath::FromASCII(script_path2).BaseName().value());
    }

    extensions::ChromeTestExtensionLoader extension_loader(
        browser()->profile());
    scoped_refptr<const extensions::Extension> extension =
        extension_loader.LoadExtension(test_extension_dir.UnpackedPath());
    WakeUpServiceWorker(*extension, *browser()->profile());
    ProgrammaticallyCreateOffscreenDocument(*extension, *browser()->profile());
    extensions::OffscreenDocumentHost* offscreen_document =
        extensions::OffscreenDocumentManager::Get(browser()->profile())
            ->GetOffscreenDocumentForExtension(*extension);
    ASSERT_TRUE(offscreen_document) << "Offscreen document not created.";
    content::WaitForLoadStop(offscreen_document->host_contents());

    TitleWatcher watcher(offscreen_document->host_contents(), u"pass");
    watcher.AlsoWaitForTitle(u"fail");
    const std::u16string title = watcher.WaitAndGetTitle();
    ASSERT_EQ(u"pass", title)
        << content::EvalJs(offscreen_document->host_contents(), R"(
             JSON.stringify({
               documentUserAgent: navigator.userAgent,
               remoteUserAgent: window.remoteUserAgent || ''
             }))")
               .ExtractString();
    const auto* persona =
        fingerprint_browser::GetPersonaForProfile(browser()->profile());
    ASSERT_TRUE(persona);
    EXPECT_EQ(persona->user_agent,
              content::EvalJs(offscreen_document->host_contents(),
                              "navigator.userAgent")
                  .ExtractString());
    if (!script_path2.empty()) {
      EXPECT_EQ(persona->user_agent,
                content::EvalJs(offscreen_document->host_contents(),
                                "window.sharedWorkerUserAgent")
                    .ExtractString());
    }
  }
#endif

 private:
  content::ContentMockCertVerifier mock_cert_verifier_;
  std::unique_ptr<net::EmbeddedTestServer> https_server_;
  std::vector<std::string> user_agents_;
  std::vector<std::string> request_paths_;
  std::vector<net::test_server::HttpRequest::HeaderMap> request_headers_;
  base::test::ScopedFeatureList feature_list_;
};

// Tests results of farbling user agent
IN_PROC_BROWSER_TEST_F(BraveNavigatorUserAgentFarblingBrowserTest,
                       FarbleNavigatorUserAgent) {
  std::u16string expected_title(u"pass");
  std::string domain_b = "b.com";
  std::string domain_z = "z.com";
  GURL url_b = https_server()->GetURL(domain_b, "/simple.html");
  GURL url_z = https_server()->GetURL(domain_z, "/simple.html");
  // get real navigator.userAgent
  std::string unfarbled_ua = embedder_support::GetUserAgent();
  const auto* persona =
      fingerprint_browser::GetPersonaForProfile(browser()->profile());
  ASSERT_TRUE(persona);
  ASSERT_NE(unfarbled_ua, persona->user_agent);
  // Farbling level: off
  AllowFingerprinting(domain_b);
  ClearObservedRequests();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url_b));
  // HTTP User-Agent header we just sent in that request should be the same as
  // the unfarbled user agent
  ExpectLastHeaderForPathEquals("/simple.html", "user-agent", unfarbled_ua);
  auto off_ua_b = EvalJs(contents(), kUserAgentScript);
  // user agent should be the same as the unfarbled user agent
  EXPECT_EQ(unfarbled_ua, off_ua_b);
  AllowFingerprinting(domain_z);
  ClearObservedRequests();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url_z));
  // HTTP User-Agent header we just sent in that request should be the same as
  // the unfarbled user agent
  ExpectLastHeaderForPathEquals("/simple.html", "user-agent", unfarbled_ua);
  auto off_ua_z = EvalJs(contents(), kUserAgentScript);
  // user agent should be the same on every domain if farbling is off
  EXPECT_EQ(unfarbled_ua, off_ua_z);

  // Farbling level: default
  SetFingerprintingDefault(domain_b);
  ClearObservedRequests();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url_b));
  ExpectLastHeaderForPathEquals("/simple.html", "user-agent",
                                persona->user_agent);
  std::string default_ua_b =
      EvalJs(contents(), kUserAgentScript).ExtractString();
  EXPECT_EQ(persona->user_agent, default_ua_b);
  SetFingerprintingDefault(domain_z);
  ClearObservedRequests();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url_z));
  ExpectLastHeaderForPathEquals("/simple.html", "user-agent",
                                persona->user_agent);
  std::string default_ua_z =
      EvalJs(contents(), kUserAgentScript).ExtractString();
  EXPECT_EQ(persona->user_agent, default_ua_z);

  // Farbling level: maximum
  BlockFingerprinting(domain_b);
  ClearObservedRequests();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url_b));
  ExpectLastHeaderForPathEquals("/simple.html", "user-agent",
                                persona->user_agent);
  auto max_ua_b = EvalJs(contents(), kUserAgentScript);
  EXPECT_EQ(persona->user_agent, max_ua_b);
  BlockFingerprinting(domain_z);
  ClearObservedRequests();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url_z));
  ExpectLastHeaderForPathEquals("/simple.html", "user-agent",
                                persona->user_agent);
  auto max_ua_z = EvalJs(contents(), kUserAgentScript);
  EXPECT_EQ(persona->user_agent, max_ua_z);

  // (farbling level is still maximum)
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      https_server()->GetURL(domain_b, "/navigator/workers-useragent.html")));
  EXPECT_EQ(persona->user_agent, last_requested_http_user_agent());
  TitleWatcher watcher3(contents(), expected_title);
  EXPECT_EQ(expected_title, watcher3.WaitAndGetTitle());

  ClearObservedRequests();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), https_server()->GetURL(
                     domain_b, "/navigator/workers-useragent-network.html")));
  TitleWatcher worker_network_watcher(contents(), expected_title);
  EXPECT_EQ(expected_title, worker_network_watcher.WaitAndGetTitle());
  auto dedicated_worker_user_agent = last_requested_header_for_path(
      "/navigator/workers-useragent-network.js", "user-agent");
  ASSERT_TRUE(dedicated_worker_user_agent);
  EXPECT_EQ(persona->user_agent, *dedicated_worker_user_agent);

  // (farbling level is still maximum)
  ClearObservedRequests();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), https_server()->GetURL(
                     domain_b, "/navigator/service-workers-useragent.html")));
  EXPECT_EQ(persona->user_agent, last_requested_http_user_agent());
  TitleWatcher watcher4(contents(), expected_title);
  EXPECT_EQ(expected_title, watcher4.WaitAndGetTitle());
  auto service_worker_user_agent = last_requested_header_for_path(
      "/navigator/service-workers-useragent.js", "user-agent");
  ASSERT_TRUE(service_worker_user_agent);
  EXPECT_EQ(persona->user_agent, *service_worker_user_agent);

  // (farbling level is still maximum)
  ClearObservedRequests();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), https_server()->GetURL(
                     domain_b, "/navigator/shared-workers-useragent.html")));
  EXPECT_EQ(persona->user_agent, last_requested_http_user_agent());
  TitleWatcher watcher5(contents(), expected_title);
  EXPECT_EQ(expected_title, watcher5.WaitAndGetTitle());
  auto shared_worker_user_agent = last_requested_header_for_path(
      "/navigator/shared-workers-worker.js", "user-agent");
  ASSERT_TRUE(shared_worker_user_agent);
  EXPECT_EQ(persona->user_agent, *shared_worker_user_agent);

  // Farbling level: off
  // verify that user agent is reset properly after having been farbled
  AllowFingerprinting(domain_b);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url_b));
  EXPECT_EQ(last_requested_http_user_agent(), unfarbled_ua);
  auto off_ua_b2 = EvalJs(contents(), kUserAgentScript);
  EXPECT_EQ(off_ua_b.ExtractString(), off_ua_b2);
}

// Tests results of farbling user agent in iframes
IN_PROC_BROWSER_TEST_F(BraveNavigatorUserAgentFarblingBrowserTest,
                       FarbleNavigatorUserAgentIframe) {
  std::u16string expected_title(u"pass");
  std::string domain_b = "b.com";
  GURL url_b = https_server()->GetURL(domain_b, "/simple.html");
  BlockFingerprinting(domain_b);

  // test that local iframes inherit the farbled user agent
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      https_server()->GetURL(domain_b, "/navigator/ua-local-iframe.html")));
  TitleWatcher watcher1(contents(), expected_title);
  EXPECT_EQ(expected_title, watcher1.WaitAndGetTitle());

  // test that remote iframes inherit the farbled user agent
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      https_server()->GetURL(domain_b, "/navigator/ua-remote-iframe.html")));
  TitleWatcher watcher2(contents(), expected_title);
  EXPECT_EQ(expected_title, watcher2.WaitAndGetTitle());

  // test that dynamic iframes inherit the farbled user agent
  // 7 variations based on https://arkenfox.github.io/TZP/tzp.html
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      https_server()->GetURL(domain_b, "/navigator/ua-dynamic-iframe-1.html")));
  TitleWatcher dynamic_iframe_1_watcher(contents(), expected_title);
  EXPECT_EQ(expected_title, dynamic_iframe_1_watcher.WaitAndGetTitle());

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      https_server()->GetURL(domain_b, "/navigator/ua-dynamic-iframe-2.html")));
  TitleWatcher dynamic_iframe_2_watcher(contents(), expected_title);
  EXPECT_EQ(expected_title, dynamic_iframe_2_watcher.WaitAndGetTitle());

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      https_server()->GetURL(domain_b, "/navigator/ua-dynamic-iframe-3.html")));
  TitleWatcher dynamic_iframe_3_watcher(contents(), expected_title);
  EXPECT_EQ(expected_title, dynamic_iframe_3_watcher.WaitAndGetTitle());

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      https_server()->GetURL(domain_b, "/navigator/ua-dynamic-iframe-4.html")));
  TitleWatcher dynamic_iframe_4_watcher(contents(), expected_title);
  EXPECT_EQ(expected_title, dynamic_iframe_4_watcher.WaitAndGetTitle());

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      https_server()->GetURL(domain_b, "/navigator/ua-dynamic-iframe-5.html")));
  TitleWatcher dynamic_iframe_5_watcher(contents(), expected_title);
  EXPECT_EQ(expected_title, dynamic_iframe_5_watcher.WaitAndGetTitle());

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      https_server()->GetURL(domain_b, "/navigator/ua-dynamic-iframe-6.html")));
  TitleWatcher dynamic_iframe_6_watcher(contents(), expected_title);
  EXPECT_EQ(expected_title, dynamic_iframe_6_watcher.WaitAndGetTitle());

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      https_server()->GetURL(domain_b, "/navigator/ua-dynamic-iframe-7.html")));
  TitleWatcher dynamic_iframe_7_watcher(contents(), expected_title);
  EXPECT_EQ(expected_title, dynamic_iframe_7_watcher.WaitAndGetTitle());
}

// Tests results of farbling user agent metadata
IN_PROC_BROWSER_TEST_F(BraveNavigatorUserAgentFarblingBrowserTest,
                       FarbleNavigatorUserAgentModel) {
  GURL url_b = https_server()->GetURL("b.com", "/navigator/useragentdata.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url_b));
  std::u16string expected_title(u"pass");
  TitleWatcher watcher(contents(), expected_title);
  EXPECT_EQ(expected_title, watcher.WaitAndGetTitle());
}

IN_PROC_BROWSER_TEST_F(BraveNavigatorUserAgentFarblingBrowserTest,
                       PersonaUserAgentAndClientHints) {
  constexpr char kDomain[] = "persona-ua.test";
  const auto* persona =
      fingerprint_browser::GetPersonaForProfile(browser()->profile());
  ASSERT_TRUE(persona);

  SetFingerprintingDefault(kDomain);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), https_server()->GetURL(kDomain, "/simple.html")));

  EXPECT_EQ(persona->user_agent, last_requested_http_user_agent());
  EXPECT_EQ(persona->user_agent,
            EvalJs(contents(), kUserAgentScript).ExtractString());

  const auto sec_ch_ua = last_requested_header("sec-ch-ua");
  ASSERT_TRUE(sec_ch_ua);
  for (const auto& brand : persona->ua_metadata.brands) {
    EXPECT_NE(std::string::npos, sec_ch_ua->find(brand.brand));
    EXPECT_NE(std::string::npos, sec_ch_ua->find(brand.version));
  }
  const auto sec_ch_ua_platform = last_requested_header("sec-ch-ua-platform");
  ASSERT_TRUE(sec_ch_ua_platform);
  EXPECT_EQ(base::StrCat({"\"", persona->ua_metadata.platform, "\""}),
            *sec_ch_ua_platform);
  const auto sec_ch_ua_mobile = last_requested_header("sec-ch-ua-mobile");
  ASSERT_TRUE(sec_ch_ua_mobile);
  EXPECT_EQ(persona->ua_metadata.mobile ? "?1" : "?0", *sec_ch_ua_mobile);

  const content::EvalJsResult result =
      EvalJs(contents(), kPersonaGetHighEntropyValuesScript);
  const base::DictValue& values = result.ExtractDict();
  const std::string* platform = values.FindString("platform");
  ASSERT_NE(nullptr, platform);
  EXPECT_EQ(persona->ua_metadata.platform, *platform);
  const std::string* platform_version = values.FindString("platformVersion");
  ASSERT_NE(nullptr, platform_version);
  EXPECT_EQ(persona->ua_metadata.platform_version, *platform_version);
  const std::string* architecture = values.FindString("architecture");
  ASSERT_NE(nullptr, architecture);
  EXPECT_EQ(persona->ua_metadata.architecture, *architecture);
  const std::string* bitness = values.FindString("bitness");
  ASSERT_NE(nullptr, bitness);
  EXPECT_EQ(persona->ua_metadata.bitness, *bitness);
  const std::string* ua_full_version = values.FindString("uaFullVersion");
  ASSERT_NE(nullptr, ua_full_version);
  EXPECT_EQ(persona->ua_metadata.full_version, *ua_full_version);

  const base::ListValue* brands_list = values.FindList("brands");
  ASSERT_NE(nullptr, brands_list);
  CheckPersonaUserAgentMetadataVersionsList(*brands_list,
                                            persona->ua_metadata.brands);
  const base::ListValue* full_version_list = values.FindList("fullVersionList");
  ASSERT_NE(nullptr, full_version_list);
  CheckPersonaUserAgentMetadataVersionsList(
      *full_version_list, persona->ua_metadata.full_version_list);
}

IN_PROC_BROWSER_TEST_F(BraveNavigatorUserAgentFarblingBrowserTest,
                       PersonaWorkersUseProfilePersona) {
  constexpr char kDomain[] = "persona-workers.test";
  const auto* persona =
      fingerprint_browser::GetPersonaForProfile(browser()->profile());
  ASSERT_TRUE(persona);

  SetFingerprintingDefault(kDomain);
  const GURL workers_url =
      https_server()->GetURL(kDomain, kPersonaWorkerPagePath);
  brave_shields::SetWebcompatEnabled(
      content_settings(), ContentSettingsType::BRAVE_WEBCOMPAT_CANVAS, true,
      workers_url, g_browser_process->local_state());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), workers_url));

  const content::EvalJsResult result = EvalJs(contents(), R"(
    (async () => {
      const hash = (bytes) => {
        let value = 0;
        for (let i = 0; i < bytes.length; ++i) {
          value = (value + bytes[i] * ((i % 8191) + 1)) % 1000000007;
        }
        return value;
      };
      const fingerprint = () => {
        const canvas = document.createElement('canvas');
        canvas.width = 32;
        canvas.height = 32;
        const context = canvas.getContext('2d');
        context.fillStyle = 'rgb(255, 0, 0)';
        context.fillRect(0, 0, canvas.width, canvas.height);
        const webglCanvas = document.createElement('canvas');
        const gl = webglCanvas.getContext('webgl');
        const debug = gl?.getExtension('WEBGL_debug_renderer_info');
        return {
          userAgent: navigator.userAgent,
          canvas: hash(context.getImageData(0, 0, canvas.width, canvas.height).data),
          renderer: debug ? gl.getParameter(debug.UNMASKED_RENDERER_WEBGL) : '',
          audioApiExposed: typeof AudioContext !== 'undefined' ||
              typeof OfflineAudioContext !== 'undefined'
        };
      };
      const dedicated = await new Promise((resolve, reject) => {
        const worker = new Worker('/persona-dedicated-worker.js');
        worker.onmessage = (event) => {
          worker.terminate();
          resolve(event.data);
        };
        worker.onerror = (event) => {
          worker.terminate();
          reject(event.message);
        };
        worker.postMessage('fingerprint');
      });
      const shared = await new Promise((resolve, reject) => {
        const worker = new SharedWorker('/persona-shared-worker.js');
        worker.port.onmessage = (event) => resolve(event.data);
        worker.port.onmessageerror = () => reject('shared worker error');
        worker.port.start();
        worker.port.postMessage('fingerprint');
      });
      const registration = await navigator.serviceWorker.register(
          '/persona-service-worker.js');
      await navigator.serviceWorker.ready;
      const service = await new Promise((resolve, reject) => {
        const active = registration.active;
        if (!active) {
          reject('service worker did not activate');
          return;
        }
        const channel = new MessageChannel();
        channel.port1.onmessage = (event) => resolve(event.data);
        channel.port1.onmessageerror = () => reject('service worker error');
        active.postMessage('fingerprint', [channel.port2]);
      });
      return {documentFingerprint: fingerprint(), dedicated, shared, service};
    })()
  )");
  const base::DictValue& fingerprints = result.ExtractDict();
  const base::DictValue* document_fingerprint =
      fingerprints.FindDict("documentFingerprint");
  const base::DictValue* dedicated_result = fingerprints.FindDict("dedicated");
  const base::DictValue* shared = fingerprints.FindDict("shared");
  const base::DictValue* service = fingerprints.FindDict("service");
  ASSERT_NE(nullptr, document_fingerprint);
  ASSERT_NE(nullptr, dedicated_result);
  ASSERT_NE(nullptr, shared);
  ASSERT_NE(nullptr, service);
  const base::DictValue* dedicated = dedicated_result->FindDict("worker");
  const base::DictValue* nested = dedicated_result->FindDict("nested");
  ASSERT_NE(nullptr, dedicated);
  ASSERT_NE(nullptr, nested);

  const std::optional<int> document_canvas =
      document_fingerprint->FindInt("canvas");
  ASSERT_TRUE(document_canvas);
  ExpectDictString(*document_fingerprint, "userAgent", persona->user_agent);
  ExpectDictString(*document_fingerprint, "renderer", persona->webgl.renderer);
  ExpectDictBool(*document_fingerprint, "audioApiExposed", true);
  for (const base::DictValue* worker : {dedicated, nested, shared, service}) {
    ExpectDictString(*worker, "userAgent", persona->user_agent);
    ExpectDictInt(*worker, "canvas", *document_canvas);
    ExpectDictString(*worker, "renderer", persona->webgl.renderer);
    ExpectDictBool(*worker, "audioApiExposed", false);
  }

  ExpectLastHeaderForPathEquals(kPersonaWorkerPagePath, "user-agent",
                                persona->user_agent);
  ExpectLastHeaderForPathEquals(kPersonaDedicatedWorkerPath, "user-agent",
                                persona->user_agent);
  ExpectLastHeaderForPathEquals(kPersonaNestedWorkerPath, "user-agent",
                                persona->user_agent);
  ExpectLastHeaderForPathEquals(kPersonaSharedWorkerPath, "user-agent",
                                persona->user_agent);
  ExpectLastHeaderForPathEquals(kPersonaServiceWorkerPath, "user-agent",
                                persona->user_agent);
}

IN_PROC_BROWSER_TEST_F(BraveNavigatorUserAgentFarblingBrowserTest,
                       PersonaScreenAndWindowValues) {
  constexpr char kDomain[] = "persona-screen.test";
  const auto* persona =
      fingerprint_browser::GetPersonaForProfile(browser()->profile());
  ASSERT_TRUE(persona);
  EXPECT_LE(persona->screen.avail_width, persona->screen.width);
  EXPECT_LE(persona->screen.avail_height, persona->screen.height);

  SetFingerprintingDefault(kDomain);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), https_server()->GetURL(kDomain, "/simple.html")));

  const content::EvalJsResult result = EvalJs(
      contents(),
      content::JsReplace(
          R"(({
                        screenWidth: screen.width,
                        screenHeight: screen.height,
                        availWidth: screen.availWidth,
                        availHeight: screen.availHeight,
                        colorDepth: screen.colorDepth,
                        pixelDepth: screen.pixelDepth,
                        devicePixelRatio: window.devicePixelRatio,
                        screenX: window.screenX,
                        screenY: window.screenY,
                        screenLeft: window.screenLeft,
                        screenTop: window.screenTop,
                        cssDeviceWidth: matchMedia($1).matches,
                        cssDeviceHeight: matchMedia($2).matches,
                        cssResolution: matchMedia($3).matches
                      }))",
          absl::StrFormat("(device-width: %dpx)", persona->screen.width),
          absl::StrFormat("(device-height: %dpx)", persona->screen.height),
          absl::StrFormat("(resolution: %gdppx)",
                          persona->screen.device_scale_factor)));
  const base::DictValue& values = result.ExtractDict();
  ExpectDictInt(values, "screenWidth", persona->screen.width);
  ExpectDictInt(values, "screenHeight", persona->screen.height);
  ExpectDictInt(values, "availWidth", persona->screen.avail_width);
  ExpectDictInt(values, "availHeight", persona->screen.avail_height);
  ExpectDictInt(values, "colorDepth", persona->screen.color_depth);
  ExpectDictInt(values, "pixelDepth", persona->screen.color_depth);
  ExpectDictDouble(values, "devicePixelRatio",
                   persona->screen.device_scale_factor);
  ExpectDictInt(values, "screenX", persona->screen.window_x);
  ExpectDictInt(values, "screenY", persona->screen.window_y);
  ExpectDictInt(values, "screenLeft", persona->screen.window_x);
  ExpectDictInt(values, "screenTop", persona->screen.window_y);
  ExpectDictBool(values, "cssDeviceWidth", true);
  ExpectDictBool(values, "cssDeviceHeight", true);
  ExpectDictBool(values, "cssResolution", true);

  const content::EvalJsResult event_result = EvalJs(contents(), R"(
    (() => {
      const mouse = new MouseEvent('mousemove', {
        view: window,
        clientX: 17,
        clientY: 23,
        screenX: 9999,
        screenY: 8888
      });
      const pointer = new PointerEvent('pointermove', {
        view: window,
        clientX: 19.5,
        clientY: 29.25,
        screenX: 9999,
        screenY: 8888
      });
      const touch = new Touch({
        identifier: 1,
        target: document.body,
        clientX: 31,
        clientY: 37,
        screenX: 9999,
        screenY: 8888,
        pageX: 31,
        pageY: 37
      });
      return {
        mouseOffsetX: mouse.screenX - mouse.clientX,
        mouseOffsetY: mouse.screenY - mouse.clientY,
        pointerOffsetX: pointer.screenX - pointer.clientX,
        pointerOffsetY: pointer.screenY - pointer.clientY,
        touchOffsetX: touch.screenX - touch.clientX,
        touchOffsetY: touch.screenY - touch.clientY,
        touchTargetIsElement: touch.target === document.body
      };
    })()
  )");
  const base::DictValue& event_values = event_result.ExtractDict();
  ExpectDictDouble(event_values, "mouseOffsetX", persona->screen.window_x);
  ExpectDictDouble(event_values, "mouseOffsetY", persona->screen.window_y);
  ExpectDictDouble(event_values, "pointerOffsetX", persona->screen.window_x);
  ExpectDictDouble(event_values, "pointerOffsetY", persona->screen.window_y);
  ExpectDictDouble(event_values, "touchOffsetX", persona->screen.window_x);
  ExpectDictDouble(event_values, "touchOffsetY", persona->screen.window_y);
  ExpectDictBool(event_values, "touchTargetIsElement", true);
}

IN_PROC_BROWSER_TEST_F(BraveNavigatorUserAgentFarblingBrowserTest,
                       PersonaAdditionalFingerprintSurfaces) {
  constexpr char kDomain[] = "persona-surfaces.test";
  const auto* persona =
      fingerprint_browser::GetPersonaForProfile(browser()->profile());
  ASSERT_TRUE(persona);

  SetFingerprintingDefault(kDomain);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), https_server()->GetURL(kDomain, "/simple.html")));

  const content::EvalJsResult result = EvalJs(contents(), R"(
    (() => {
      const readPixels = (contextName) => {
        const canvas = document.createElement('canvas');
        canvas.width = 32;
        canvas.height = 32;
        const gl = canvas.getContext(contextName);
        if (!gl) {
          return {available: false};
        }

        gl.clearColor(1, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);

        const expected = (index) => (index % 4 === 0 || index % 4 === 3)
            ? 255
            : 0;
        const a = new Uint8Array(canvas.width * canvas.height * 4);
        const b = new Uint8Array(canvas.width * canvas.height * 4);
        gl.readPixels(0, 0, canvas.width, canvas.height, gl.RGBA,
                      gl.UNSIGNED_BYTE, a);
        gl.readPixels(0, 0, canvas.width, canvas.height, gl.RGBA,
                      gl.UNSIGNED_BYTE, b);
        return {
          available: true,
          stable: a.every((value, index) => value === b[index]),
          perturbed: a.some((value, index) => value !== expected(index))
        };
      };

      const webgl = readPixels('webgl');
      const webgl2 = readPixels('webgl2');
      const webglCanvas = document.createElement('canvas');
      const webglContext = webglCanvas.getContext('webgl');
      const debugInfo = webglContext
          ? webglContext.getExtension('WEBGL_debug_renderer_info')
          : null;
      let webgl2PixelPackBlocked = true;
      if (webgl2.available) {
        const canvas = document.createElement('canvas');
        canvas.width = 1;
        canvas.height = 1;
        const gl = canvas.getContext('webgl2');
        const buffer = gl.createBuffer();
        gl.bindBuffer(gl.PIXEL_PACK_BUFFER, buffer);
        gl.bufferData(gl.PIXEL_PACK_BUFFER, 4, gl.STREAM_READ);
        gl.readPixels(0, 0, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, 0);
        webgl2PixelPackBlocked = gl.getError() === gl.INVALID_OPERATION;
        gl.bindBuffer(gl.PIXEL_PACK_BUFFER, null);
      }

      const gamepads = navigator.getGamepads
          ? Array.from(navigator.getGamepads())
          : [];
      return {
        maxTouchPoints: navigator.maxTouchPoints,
        gamepadLength: gamepads.length,
        presentGamepads: gamepads.filter(Boolean).length,
        webglAvailable: webgl.available,
        webglPerturbed: webgl.perturbed === true,
        webglStable: webgl.stable === true,
        webglDebugAvailable: !!debugInfo,
        webglVendor: debugInfo
            ? webglContext.getParameter(debugInfo.UNMASKED_VENDOR_WEBGL)
            : '',
        webglRenderer: debugInfo
            ? webglContext.getParameter(debugInfo.UNMASKED_RENDERER_WEBGL)
            : '',
        webgl2PboBlocked: webgl2PixelPackBlocked
      };
    })()
  )");
  const base::DictValue& values = result.ExtractDict();
  ExpectDictInt(values, "maxTouchPoints", persona->max_touch_points);
  ExpectDictInt(values, "gamepadLength", 4);
  ExpectDictInt(values, "presentGamepads", 0);
  ExpectDictBool(values, "webglAvailable", true);
  ExpectDictBool(values, "webglPerturbed", true);
  ExpectDictBool(values, "webglStable", true);
  ExpectDictBool(values, "webglDebugAvailable", true);
  ExpectDictString(values, "webglVendor", persona->webgl.vendor);
  ExpectDictString(values, "webglRenderer", persona->webgl.renderer);
  ExpectDictBool(values, "webgl2PboBlocked", true);
}

IN_PROC_BROWSER_TEST_F(BraveNavigatorUserAgentFarblingBrowserTest,
                       PersonaFontMediaAndSpeechSurfaces) {
  constexpr char kDomain[] = "persona-rich-surfaces.test";
  base::DictValue persona_pref =
      browser()
          ->profile()
          ->GetPrefs()
          ->GetDict(fingerprint_browser::prefs::kPersona)
          .Clone();
  base::ListValue* plugins = persona_pref.FindList("plugins");
  ASSERT_TRUE(plugins);
  ASSERT_FALSE(plugins->empty());
  base::DictValue& first_plugin = plugins->front().GetDict();
  first_plugin.Set("name", "Persona PDF Test Plugin");
  first_plugin.Set("filename", "persona-pdf-test-plugin");
  base::ListValue* mime_types = first_plugin.FindList("mime_types");
  ASSERT_TRUE(mime_types);
  base::DictValue custom_mime;
  custom_mime.Set("type", "application/x-persona-test");
  custom_mime.Set("description", "Persona Test Format");
  base::ListValue custom_suffixes;
  custom_suffixes.Append("persona");
  custom_mime.Set("suffixes", std::move(custom_suffixes));
  mime_types->Append(std::move(custom_mime));
  browser()->profile()->GetPrefs()->SetDict(
      fingerprint_browser::prefs::kPersona, std::move(persona_pref));
  auto* persona_service =
      fingerprint_browser::PersonaServiceFactory::GetForProfile(
          browser()->profile());
  ASSERT_TRUE(persona_service);
  ASSERT_TRUE(persona_service->EnsurePersona())
      << persona_service->last_error();
  const auto* persona =
      fingerprint_browser::GetPersonaForProfile(browser()->profile());
  ASSERT_TRUE(persona);

  SetFingerprintingDefault(kDomain);
  const GURL url = https_server()->GetURL(kDomain, "/simple.html");
  content_settings()->SetContentSettingDefaultScope(
      url, url, ContentSettingsType::LOCAL_FONTS, CONTENT_SETTING_ALLOW);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));

  const content::EvalJsResult result = EvalJs(contents(), R"(
    (async () => {
      const summarizeVoices = () => new Promise((resolve) => {
        const current = speechSynthesis.getVoices();
        if (current.length) {
          resolve(current);
          return;
        }
        let done = false;
        const finish = () => {
          if (done) {
            return;
          }
          done = true;
          resolve(speechSynthesis.getVoices());
        };
        speechSynthesis.addEventListener('voiceschanged', finish, {once: true});
        setTimeout(finish, 1000);
      });

      const devices = await navigator.mediaDevices.enumerateDevices();
      const openedInputKinds = [];
      let mediaDeviceRoundTrip = true;
      for (const device of devices.filter(
          device => device.kind === 'audioinput' ||
              device.kind === 'videoinput')) {
        try {
          const constraints = device.kind === 'audioinput'
              ? {audio: {
                  deviceId: {exact: device.deviceId},
                  groupId: {exact: device.groupId},
                }}
              : {video: {
                  deviceId: {exact: device.deviceId},
                  groupId: {exact: device.groupId},
                }};
          const stream = await navigator.mediaDevices.getUserMedia(constraints);
          openedInputKinds.push(...stream.getTracks().map(track => track.kind));
          stream.getTracks().forEach(track => track.stop());
        } catch (error) {
          mediaDeviceRoundTrip = false;
        }
      }
      const voices = await summarizeVoices();
      let fontAccessStatus = 'unavailable';
      let fontFamilies = [];
      if ('queryLocalFonts' in self) {
        try {
          const fonts = await self.queryLocalFonts();
          fontAccessStatus = 'ok';
          fontFamilies = Array.from(new Set(fonts.map(font => font.family)))
              .filter(Boolean)
              .sort();
        } catch (error) {
          fontAccessStatus = 'error:' + error.name;
        }
      }

      return {
        fontAccessStatus,
        fontFamilies: fontFamilies.join('\n'),
        mediaCore: devices
            .map(device => [device.kind, device.deviceId, device.groupId]
                .join('|'))
            .join('\n'),
        mediaLabels: devices.map(device => device.label).join('\n'),
        mediaDeviceRoundTrip,
        openedInputKinds: openedInputKinds.sort().join(','),
        pluginCore: Array.from(navigator.plugins)
            .map(plugin => [
              plugin.name,
              plugin.filename,
              plugin.description,
              Array.from(plugin)
                  .map(mime => [mime.type, mime.description, mime.suffixes]
                      .join('|'))
                  .join(';'),
            ].join('|'))
            .join('\n'),
        mimeCore: Array.from(navigator.mimeTypes)
            .map(mime => [mime.type, mime.description, mime.suffixes].join('|'))
            .sort()
            .join('\n'),
        speechVoices: voices
            .map(voice => [
              voice.name,
              voice.voiceURI,
              voice.lang,
              String(voice.localService),
              String(voice.default),
            ].join('|'))
            .join('\n'),
      };
    })()
  )");
  const base::DictValue& values = result.ExtractDict();

  std::string expected_media_core;
  std::string expected_media_labels;
  std::string expected_empty_media_labels;
  for (size_t i = 0; i < persona->media_devices.size(); ++i) {
    const auto& device = persona->media_devices[i];
    if (i > 0) {
      expected_media_core += "\n";
      expected_media_labels += "\n";
      expected_empty_media_labels += "\n";
    }
    expected_media_core += base::StrCat(
        {fingerprint_browser::PersonaMediaDeviceKindToString(device.kind), "|",
         device.device_id, "|", device.group_id});
    expected_media_labels += device.label;
  }

  std::string expected_speech_voices;
  for (size_t i = 0; i < persona->speech_voices.size(); ++i) {
    const auto& voice = persona->speech_voices[i];
    if (i > 0) {
      expected_speech_voices += "\n";
    }
    expected_speech_voices +=
        base::StrCat({voice.name, "|", voice.voice_uri, "|", voice.lang, "|",
                      voice.local_service ? "true" : "false", "|",
                      voice.is_default ? "true" : "false"});
  }

  std::string expected_plugins;
  for (size_t i = 0; i < persona->plugins.size(); ++i) {
    const auto& plugin = persona->plugins[i];
    if (i > 0) {
      expected_plugins += "\n";
    }
    expected_plugins += base::StrCat(
        {plugin.name, "|", plugin.filename, "|", plugin.description, "|"});
    for (size_t j = 0; j < plugin.mime_types.size(); ++j) {
      if (j > 0) {
        expected_plugins += ";";
      }
      const auto& mime = plugin.mime_types[j];
      expected_plugins += base::StrCat({mime.type, "|", mime.description, "|",
                                        base::JoinString(mime.suffixes, ",")});
    }
  }
  std::set<std::string> expected_mime_set;
  for (const auto& plugin : persona->plugins) {
    for (const auto& mime : plugin.mime_types) {
      expected_mime_set.insert(
          base::StrCat({mime.type, "|", mime.description, "|",
                        base::JoinString(mime.suffixes, ",")}));
    }
  }
  const std::string expected_mimes =
      base::JoinString(std::vector<std::string>(expected_mime_set.begin(),
                                                expected_mime_set.end()),
                       "\n");

  const std::string* media_core = values.FindString("mediaCore");
  const std::string* media_labels = values.FindString("mediaLabels");
  const std::string* speech_voices = values.FindString("speechVoices");
  const std::string* font_access_status = values.FindString("fontAccessStatus");
  const std::string* font_families = values.FindString("fontFamilies");
  const std::string* opened_input_kinds = values.FindString("openedInputKinds");
  const std::string* plugin_core = values.FindString("pluginCore");
  const std::string* mime_core = values.FindString("mimeCore");
  ASSERT_TRUE(media_core);
  ASSERT_TRUE(media_labels);
  ASSERT_TRUE(speech_voices);
  ASSERT_TRUE(font_access_status);
  ASSERT_TRUE(font_families);
  ASSERT_TRUE(opened_input_kinds);
  ASSERT_TRUE(plugin_core);
  ASSERT_TRUE(mime_core);
  EXPECT_EQ(expected_media_core, *media_core);
  EXPECT_TRUE(*media_labels == expected_empty_media_labels ||
              *media_labels == expected_media_labels)
      << *media_labels;
  EXPECT_EQ(expected_speech_voices, *speech_voices);
  ExpectDictBool(values, "mediaDeviceRoundTrip", true);
  EXPECT_EQ("audio,video", *opened_input_kinds);
  EXPECT_EQ(expected_plugins, *plugin_core);
  EXPECT_EQ(expected_mimes, *mime_core);
  ASSERT_EQ("ok", *font_access_status);

  for (const auto& family :
       base::SplitString(*font_families, "\n", base::KEEP_WHITESPACE,
                         base::SPLIT_WANT_NONEMPTY)) {
    EXPECT_TRUE(std::ranges::contains(persona->fonts, family)) << family;
  }
}

IN_PROC_BROWSER_TEST_F(BraveNavigatorUserAgentFarblingBrowserTest,
                       PersonaCanvasOutputsAreStable) {
  constexpr char kDomain[] = "persona-canvas.test";
  ASSERT_TRUE(fingerprint_browser::GetPersonaForProfile(browser()->profile()));

  SetFingerprintingDefault(kDomain);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), https_server()->GetURL(kDomain, "/simple.html")));
  const content::EvalJsResult persona_result =
      EvalJs(contents(), kPersonaCanvasFingerprintScript);
  const base::DictValue& persona_values = persona_result.ExtractDict();
  ExpectDictBool(persona_values, "imageDataStable", true);
  ExpectDictBool(persona_values, "dataUrlStable", true);
  ExpectDictBool(persona_values, "blobStable", true);
  const std::optional<int> persona_image_data_hash =
      persona_values.FindInt("imageDataHash");
  const std::optional<int> persona_data_url_hash =
      persona_values.FindInt("dataUrlHash");
  const std::optional<int> persona_blob_hash =
      persona_values.FindInt("blobHash");
  ASSERT_TRUE(persona_image_data_hash.has_value());
  ASSERT_TRUE(persona_data_url_hash.has_value());
  ASSERT_TRUE(persona_blob_hash.has_value());
  EXPECT_NE(-1, *persona_blob_hash);

  AllowFingerprinting(kDomain);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), https_server()->GetURL(kDomain, "/simple.html")));
  const content::EvalJsResult real_result =
      EvalJs(contents(), kPersonaCanvasFingerprintScript);
  const base::DictValue& real_values = real_result.ExtractDict();
  ExpectDictBool(real_values, "imageDataStable", true);
  ExpectDictBool(real_values, "dataUrlStable", true);
  ExpectDictBool(real_values, "blobStable", true);
  const std::optional<int> real_image_data_hash =
      real_values.FindInt("imageDataHash");
  const std::optional<int> real_data_url_hash =
      real_values.FindInt("dataUrlHash");
  const std::optional<int> real_blob_hash = real_values.FindInt("blobHash");
  ASSERT_TRUE(real_image_data_hash.has_value());
  ASSERT_TRUE(real_data_url_hash.has_value());
  ASSERT_TRUE(real_blob_hash.has_value());
  EXPECT_NE(-1, *real_blob_hash);

  EXPECT_NE(*real_image_data_hash, *persona_image_data_hash);
  EXPECT_NE(*real_data_url_hash, *persona_data_url_hash);
  EXPECT_NE(*real_blob_hash, *persona_blob_hash);
}

IN_PROC_BROWSER_TEST_F(BraveNavigatorUserAgentFarblingBrowserTest,
                       PersonaUserAgentStaysOutOfSystemNetworkContext) {
  const auto* persona =
      fingerprint_browser::GetPersonaForProfile(browser()->profile());
  ASSERT_TRUE(persona);
  const std::string real_user_agent = embedder_support::GetUserAgent();
  ASSERT_NE(real_user_agent, persona->user_agent);

  ClearObservedRequests();
  LoadURL("profile-network-context.test",
          browser()
              ->profile()
              ->GetDefaultStoragePartition()
              ->GetURLLoaderFactoryForBrowserProcess());
  EXPECT_EQ(persona->user_agent, last_requested_http_user_agent());

  ClearObservedRequests();
  LoadURL("system-network-context.test",
          g_browser_process->system_network_context_manager()
              ->GetSharedURLLoaderFactory());
  EXPECT_EQ(real_user_agent, last_requested_http_user_agent());
}

IN_PROC_BROWSER_TEST_F(BraveNavigatorUserAgentFarblingBrowserTest,
                       PersonaUserAgentRespectsShieldsForFetchRequests) {
  constexpr char kDomain[] = "persona-fetch.test";
  const auto* persona =
      fingerprint_browser::GetPersonaForProfile(browser()->profile());
  ASSERT_TRUE(persona);
  const std::string real_user_agent = embedder_support::GetUserAgent();
  const auto real_metadata = embedder_support::GetUserAgentMetadata();
  ASSERT_NE(real_user_agent, persona->user_agent);

  SetFingerprintingDefault(kDomain);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), https_server()->GetURL(kDomain, "/simple.html")));
  ClearObservedRequests();
  EXPECT_EQ("done",
            EvalJs(contents(), "fetch('/simple.html').then(() => 'done')"));
  EXPECT_EQ(persona->user_agent, last_requested_http_user_agent());
  auto sec_ch_ua_platform = last_requested_header("sec-ch-ua-platform");
  ASSERT_TRUE(sec_ch_ua_platform);
  EXPECT_EQ(base::StrCat({"\"", persona->ua_metadata.platform, "\""}),
            *sec_ch_ua_platform);
  auto sec_ch_ua_mobile = last_requested_header("sec-ch-ua-mobile");
  ASSERT_TRUE(sec_ch_ua_mobile);
  EXPECT_EQ(persona->ua_metadata.mobile ? "?1" : "?0", *sec_ch_ua_mobile);

  ClearObservedRequests();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), https_server()->GetURL(
                     kDomain, "/navigator/subresource-useragent.html")));
  TitleWatcher subresource_watcher(contents(), u"pass");
  EXPECT_EQ(u"pass", subresource_watcher.WaitAndGetTitle());
  ExpectLastHeaderForPathEquals("/navigator/subresource-useragent.js",
                                "user-agent", persona->user_agent);
  ExpectLastHeaderForPathEquals(
      "/navigator/subresource-useragent.js", "sec-ch-ua-platform",
      base::StrCat({"\"", persona->ua_metadata.platform, "\""}));
  ExpectLastHeaderForPathEquals("/navigator/subresource-useragent.js",
                                "sec-ch-ua-mobile",
                                persona->ua_metadata.mobile ? "?1" : "?0");

  ClearObservedRequests();
  EXPECT_EQ("done", LoadWithXmlHttpRequest("/simple.html?xhr=persona"));
  ExpectLastHeaderForPathEquals("/simple.html?xhr=persona", "user-agent",
                                persona->user_agent);
  ExpectLastHeaderForPathEquals(
      "/simple.html?xhr=persona", "sec-ch-ua-platform",
      base::StrCat({"\"", persona->ua_metadata.platform, "\""}));
  ExpectLastHeaderForPathEquals("/simple.html?xhr=persona", "sec-ch-ua-mobile",
                                persona->ua_metadata.mobile ? "?1" : "?0");

  AllowFingerprinting(kDomain);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), https_server()->GetURL(kDomain, "/simple.html")));
  ClearObservedRequests();
  EXPECT_EQ("done",
            EvalJs(contents(), "fetch('/simple.html').then(() => 'done')"));
  EXPECT_EQ(real_user_agent, last_requested_http_user_agent());
  sec_ch_ua_platform = last_requested_header("sec-ch-ua-platform");
  ASSERT_TRUE(sec_ch_ua_platform);
  EXPECT_EQ(base::StrCat({"\"", real_metadata.platform, "\""}),
            *sec_ch_ua_platform);
  sec_ch_ua_mobile = last_requested_header("sec-ch-ua-mobile");
  ASSERT_TRUE(sec_ch_ua_mobile);
  EXPECT_EQ(real_metadata.mobile ? "?1" : "?0", *sec_ch_ua_mobile);

  ClearObservedRequests();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), https_server()->GetURL(
                     kDomain, "/navigator/subresource-useragent.html")));
  TitleWatcher real_subresource_watcher(contents(), u"pass");
  EXPECT_EQ(u"pass", real_subresource_watcher.WaitAndGetTitle());
  ExpectLastHeaderForPathEquals("/navigator/subresource-useragent.js",
                                "user-agent", real_user_agent);
  ExpectLastHeaderForPathEquals(
      "/navigator/subresource-useragent.js", "sec-ch-ua-platform",
      base::StrCat({"\"", real_metadata.platform, "\""}));
  ExpectLastHeaderForPathEquals("/navigator/subresource-useragent.js",
                                "sec-ch-ua-mobile",
                                real_metadata.mobile ? "?1" : "?0");

  ClearObservedRequests();
  EXPECT_EQ("done", LoadWithXmlHttpRequest("/simple.html?xhr=real"));
  ExpectLastHeaderForPathEquals("/simple.html?xhr=real", "user-agent",
                                real_user_agent);
  ExpectLastHeaderForPathEquals(
      "/simple.html?xhr=real", "sec-ch-ua-platform",
      base::StrCat({"\"", real_metadata.platform, "\""}));
  ExpectLastHeaderForPathEquals("/simple.html?xhr=real", "sec-ch-ua-mobile",
                                real_metadata.mobile ? "?1" : "?0");
}

IN_PROC_BROWSER_TEST_F(BraveNavigatorUserAgentFarblingBrowserTest,
                       PersonaUserAgentRespectsWebcompatForFetchRequests) {
  constexpr char kDomain[] = "persona-fetch-webcompat.test";
  const auto* persona =
      fingerprint_browser::GetPersonaForProfile(browser()->profile());
  ASSERT_TRUE(persona);
  const std::string real_user_agent = embedder_support::GetUserAgent();
  const auto real_metadata = embedder_support::GetUserAgentMetadata();
  ASSERT_NE(real_user_agent, persona->user_agent);

  SetFingerprintingDefault(kDomain);
  AllowUserAgentWebcompat(kDomain);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), https_server()->GetURL(kDomain, "/simple.html")));

  EXPECT_EQ(real_user_agent,
            EvalJs(contents(), kUserAgentScript).ExtractString());

  ClearObservedRequests();
  EXPECT_EQ("done",
            EvalJs(contents(), "fetch('/simple.html').then(() => 'done')"));
  EXPECT_EQ(real_user_agent, last_requested_http_user_agent());
  auto sec_ch_ua_platform = last_requested_header("sec-ch-ua-platform");
  ASSERT_TRUE(sec_ch_ua_platform);
  EXPECT_EQ(base::StrCat({"\"", real_metadata.platform, "\""}),
            *sec_ch_ua_platform);
  auto sec_ch_ua_mobile = last_requested_header("sec-ch-ua-mobile");
  ASSERT_TRUE(sec_ch_ua_mobile);
  EXPECT_EQ(real_metadata.mobile ? "?1" : "?0", *sec_ch_ua_mobile);

  ClearObservedRequests();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), https_server()->GetURL(
                     kDomain, "/navigator/subresource-useragent.html")));
  TitleWatcher subresource_watcher(contents(), u"pass");
  EXPECT_EQ(u"pass", subresource_watcher.WaitAndGetTitle());
  ExpectLastHeaderForPathEquals("/navigator/subresource-useragent.js",
                                "user-agent", real_user_agent);

  ClearObservedRequests();
  EXPECT_EQ("done", LoadWithXmlHttpRequest("/simple.html?xhr=webcompat"));
  ExpectLastHeaderForPathEquals("/simple.html?xhr=webcompat", "user-agent",
                                real_user_agent);
}

IN_PROC_BROWSER_TEST_F(BraveNavigatorUserAgentFarblingBrowserTest,
                       PersonaUserAgentRespectsShieldsForWebSocketRequests) {
  const auto* persona =
      fingerprint_browser::GetPersonaForProfile(browser()->profile());
  ASSERT_TRUE(persona);
  const std::string real_user_agent = embedder_support::GetUserAgent();
  ASSERT_NE(real_user_agent, persona->user_agent);

  constexpr char kDefaultDomain[] = "persona-websocket.test";
  SetFingerprintingDefault(kDefaultDomain);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), https_server()->GetURL(kDefaultDomain, "/simple.html")));
  const content::EvalJsResult persona_headers_result =
      GetWebSocketRequestHeaders(net::test_server::GetWebSocketURL(
          *https_server(), kDefaultDomain, "/echo-request-headers"));
  const base::DictValue& persona_headers = persona_headers_result.ExtractDict();
  const std::string* persona_ws_user_agent =
      persona_headers.FindString("user-agent");
  ASSERT_NE(nullptr, persona_ws_user_agent);
  EXPECT_EQ(persona->user_agent, *persona_ws_user_agent);

  constexpr char kAllowedDomain[] = "persona-websocket-allowed.test";
  AllowFingerprinting(kAllowedDomain);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), https_server()->GetURL(kAllowedDomain, "/simple.html")));
  const content::EvalJsResult real_headers_result =
      GetWebSocketRequestHeaders(net::test_server::GetWebSocketURL(
          *https_server(), kAllowedDomain, "/echo-request-headers"));
  const base::DictValue& real_headers = real_headers_result.ExtractDict();
  const std::string* real_ws_user_agent = real_headers.FindString("user-agent");
  ASSERT_NE(nullptr, real_ws_user_agent);
  EXPECT_EQ(real_user_agent, *real_ws_user_agent);

  constexpr char kWebcompatDomain[] = "persona-websocket-webcompat.test";
  SetFingerprintingDefault(kWebcompatDomain);
  AllowUserAgentWebcompat(kWebcompatDomain);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), https_server()->GetURL(kWebcompatDomain, "/simple.html")));
  const content::EvalJsResult webcompat_headers_result =
      GetWebSocketRequestHeaders(net::test_server::GetWebSocketURL(
          *https_server(), kWebcompatDomain, "/echo-request-headers"));
  const base::DictValue& webcompat_headers =
      webcompat_headers_result.ExtractDict();
  const std::string* webcompat_ws_user_agent =
      webcompat_headers.FindString("user-agent");
  ASSERT_NE(nullptr, webcompat_ws_user_agent);
  EXPECT_EQ(real_user_agent, *webcompat_ws_user_agent);
}

// Tests results of user agent metadata brands
IN_PROC_BROWSER_TEST_F(BraveNavigatorUserAgentFarblingBrowserTest,
                       BraveIsInNavigatorUserAgentBrandList) {
  constexpr char kDomain[] = "a.com";
  AllowFingerprinting(kDomain);
  GURL url = https_server()->GetURL(kDomain, "/simple.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  std::string brands = EvalJs(contents(), kBrandScript).ExtractString();
  EXPECT_NE(std::string::npos, brands.find("Brave"));
  EXPECT_NE(std::string::npos, brands.find("Chromium"));
}

// Tests that user agent metadata versions are as expected.
IN_PROC_BROWSER_TEST_F(BraveNavigatorUserAgentFarblingBrowserTest,
                       CheckUserAgentMetadataVersions) {
  constexpr char kDomain[] = "a.com";
  AllowFingerprinting(kDomain);
  GURL url = https_server()->GetURL(kDomain, "/simple.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  const content::EvalJsResult result =
      EvalJs(contents(), kGetHighEntropyValuesScript);

  // Check brands versions
  const base::DictValue& values = result.ExtractDict();
  const base::ListValue* brands_list = values.FindList("brands");
  ASSERT_NE(nullptr, brands_list);

  // Expected major version for Brave and Chromium.
  const std::string major_version = version_info::GetMajorVersionNumber();

  CheckUserAgentMetadataVersionsList(
      *brands_list, major_version, [](const std::string& version) {
        EXPECT_EQ(std::string::npos, version.find("."));
      });

  // Check full versions
  const base::ListValue* full_version_list = values.FindList("fullVersionList");
  ASSERT_NE(nullptr, full_version_list);

  // Expected version string for Brave and Chromium.
  const std::string expected_full_version =
      base::StrCat({major_version, ".0.0.0"});

  CheckUserAgentMetadataVersionsList(
      *full_version_list, expected_full_version,
      [](const std::string& version_str) {
        base::Version version(version_str);
        for (size_t i = 0; i < version.components().size(); i++) {
          if (i > 0) {
            EXPECT_EQ(0U, version.components()[i]);
          }
        }
      });

  // Check auFullVersion
  const std::string* ua_full_version = values.FindString("uaFullVersion");
  ASSERT_NE(nullptr, ua_full_version);
  EXPECT_EQ(expected_full_version, *ua_full_version);
}

#if BUILDFLAG(ENABLE_EXTENSIONS)
IN_PROC_BROWSER_TEST_F(BraveNavigatorUserAgentFarblingBrowserTest,
                       ExtensionOffscreenDocument) {
  TestExtensionOffscreenDocument("navigator/ua-remote-iframe.html",
                                 "navigator/ua-remote-iframe.js");
}

IN_PROC_BROWSER_TEST_F(BraveNavigatorUserAgentFarblingBrowserTest,
                       ExtensionOffscreenDocumentWorker) {
  TestExtensionOffscreenDocument("navigator/workers-useragent.html",
                                 "navigator/workers-useragent.js");
}

IN_PROC_BROWSER_TEST_F(BraveNavigatorUserAgentFarblingBrowserTest,
                       ExtensionOffscreenDocumentRemoteIframe) {
  TestExtensionOffscreenDocument("navigator/workers-remote-iframe.html",
                                 "navigator/workers-remote-iframe.js");
}

IN_PROC_BROWSER_TEST_F(BraveNavigatorUserAgentFarblingBrowserTest,
                       ExtensionOffscreenDocumentSharedWorker) {
  TestExtensionOffscreenDocument("navigator/shared-workers-useragent.html",
                                 "navigator/shared-workers-useragent.js",
                                 "navigator/shared-workers-worker.js");
}

IN_PROC_BROWSER_TEST_F(BraveNavigatorUserAgentFarblingBrowserTest,
                       ExtensionServiceWorkerUsesProfilePersona) {
  constexpr char kDomain[] = "extension-worker-persona.test";
  const auto* persona =
      fingerprint_browser::GetPersonaForProfile(browser()->profile());
  ASSERT_TRUE(persona);

  SetFingerprintingDefault(kDomain);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), https_server()->GetURL(kDomain, "/simple.html")));

  constexpr char kPageFingerprintScript[] = R"(
    (() => {
      const hash = (bytes) => {
        let value = 0;
        for (let i = 0; i < bytes.length; ++i) {
          value = (value + bytes[i] * ((i % 8191) + 1)) % 1000000007;
        }
        return value;
      };
      const canvas = document.createElement('canvas');
      canvas.width = 32;
      canvas.height = 32;
      const context = canvas.getContext('2d');
      context.fillStyle = 'rgb(255, 0, 0)';
      context.fillRect(0, 0, canvas.width, canvas.height);
      const webgl_canvas = document.createElement('canvas');
      const gl = webgl_canvas.getContext('webgl');
      const debug = gl?.getExtension('WEBGL_debug_renderer_info');
      return {
        canvas: hash(context.getImageData(0, 0, canvas.width, canvas.height).data),
        renderer: debug ? gl.getParameter(debug.UNMASKED_RENDERER_WEBGL) : ''
      };
    })()
  )";
  content::EvalJsResult page_fingerprint_result =
      EvalJs(contents(), kPageFingerprintScript);
  const base::DictValue& page_fingerprint =
      page_fingerprint_result.ExtractDict();
  const std::optional<int> page_canvas = page_fingerprint.FindInt("canvas");
  const std::string* page_renderer = page_fingerprint.FindString("renderer");
  ASSERT_TRUE(page_canvas);
  ASSERT_NE(nullptr, page_renderer);
  EXPECT_EQ(persona->webgl.renderer, *page_renderer);

  extensions::TestExtensionDir test_extension_dir;
  test_extension_dir.WriteManifest(R"({
    "name": "Persona worker test",
    "manifest_version": 3,
    "version": "0.1",
    "background": {"service_worker": "background.js"}
  })");
  test_extension_dir.WriteFile(FILE_PATH_LITERAL("background.js"), "");

  extensions::ChromeTestExtensionLoader extension_loader(browser()->profile());
  scoped_refptr<const extensions::Extension> extension =
      extension_loader.LoadExtension(test_extension_dir.UnpackedPath());
  ASSERT_TRUE(extension);
  auto settings = static_cast<BraveContentBrowserClient*>(
                      content::GetContentClientForTesting()->browser())
                      ->WorkerGetBraveShieldSettings(
                          extension->GetResourceURL("background.js"),
                          browser()->profile(), nullptr);
  ASSERT_TRUE(settings);
  EXPECT_EQ(brave_shields::mojom::FarblingLevel::BALANCED,
            settings->farbling_level);
  EXPECT_TRUE(settings->has_persona_l1);
  EXPECT_TRUE(settings->has_persona_l2);
  EXPECT_EQ(persona->user_agent, settings->persona_user_agent);
  EXPECT_EQ(persona->webgl.renderer, settings->persona_webgl_renderer);
  EXPECT_EQ(fingerprint_browser::PersonaPluginNames(persona->plugins),
            settings->persona_plugin_names);
  EXPECT_EQ(fingerprint_browser::PersonaPluginFilenames(persona->plugins),
            settings->persona_plugin_filenames);
  EXPECT_EQ(fingerprint_browser::PersonaPluginDescriptions(persona->plugins),
            settings->persona_plugin_descriptions);
  EXPECT_EQ(fingerprint_browser::PersonaPluginMimeTypeCounts(persona->plugins),
            settings->persona_plugin_mime_type_counts);
  EXPECT_EQ(fingerprint_browser::PersonaMimeTypeTypes(persona->plugins),
            settings->persona_mime_type_types);
  EXPECT_EQ(fingerprint_browser::PersonaMimeTypeDescriptions(persona->plugins),
            settings->persona_mime_type_descriptions);
  EXPECT_EQ(fingerprint_browser::PersonaMimeTypeSuffixes(persona->plugins),
            settings->persona_mime_type_suffixes);
  WakeUpServiceWorker(*extension, *browser()->profile());

  constexpr char kWorkerFingerprintScript[] = R"(
    (() => {
      const hash = (bytes) => {
        let value = 0;
        for (let i = 0; i < bytes.length; ++i) {
          value = (value + bytes[i] * ((i % 8191) + 1)) % 1000000007;
        }
        return value;
      };
      const canvas = new OffscreenCanvas(32, 32);
      const context = canvas.getContext('2d');
      context.fillStyle = 'rgb(255, 0, 0)';
      context.fillRect(0, 0, canvas.width, canvas.height);
      const webgl_canvas = new OffscreenCanvas(1, 1);
      const gl = webgl_canvas.getContext('webgl');
      const debug = gl?.getExtension('WEBGL_debug_renderer_info');
      chrome.test.sendScriptResult(JSON.stringify({
        userAgent: navigator.userAgent,
        canvas: hash(context.getImageData(0, 0, canvas.width, canvas.height).data),
        renderer: debug ? gl.getParameter(debug.UNMASKED_RENDERER_WEBGL) : ''
      }));
    })();
  )";
  base::Value worker_result =
      extensions::BackgroundScriptExecutor::ExecuteScript(
          browser()->profile(), extension->id(), kWorkerFingerprintScript,
          extensions::BackgroundScriptExecutor::ResultCapture::
              kSendScriptResult);
  ASSERT_TRUE(worker_result.is_string());
  std::optional<base::Value> worker_fingerprint =
      base::JSONReader::Read(worker_result.GetString(), base::JSON_PARSE_RFC);
  ASSERT_TRUE(worker_fingerprint);
  ASSERT_TRUE(worker_fingerprint->is_dict());
  const base::DictValue& worker_values = worker_fingerprint->GetDict();
  ExpectDictString(worker_values, "userAgent", persona->user_agent);
  ExpectDictInt(worker_values, "canvas", *page_canvas);
  ExpectDictString(worker_values, "renderer", persona->webgl.renderer);
}
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)
