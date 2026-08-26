/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/run_loop.h"
#include "base/synchronization/lock.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "brave/browser/farbling/local_socks5_test_server.h"
#include "brave/browser/fingerprint_browser/fingerprint_proxy_service.h"
#include "brave/browser/fingerprint_browser/fingerprint_proxy_service_factory.h"
#include "brave/browser/fingerprint_browser/fingerprint_proxy_ui_strings.h"
#include "brave/browser/fingerprint_browser/persona_service_factory.h"
#include "brave/browser/ui/webui/brave_settings_ui.h"
#include "brave/components/fingerprint_browser/browser/persona.h"
#include "brave/components/fingerprint_browser/browser/pref_names.h"
#include "brave/components/fingerprint_browser/browser/profile_proxy_config.h"
#include "brave/components/tor/buildflags/buildflags.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/profiles/delete_profile_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_metrics.h"
#include "chrome/browser/profiles/profile_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/profile_deletion_observer.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/webui_config_map.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_devtools_protocol_client.h"
#include "net/base/net_errors.h"
#include "net/dns/mock_host_resolver.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_status_code.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "third_party/blink/public/common/renderer_preferences/renderer_preferences.h"

#if BUILDFLAG(ENABLE_TOR)
#include "brave/browser/tor/tor_profile_manager.h"
#include "brave/components/tor/tor_navigation_throttle.h"
#include "brave/net/proxy_resolution/proxy_config_service_tor.h"
#endif

namespace {

constexpr char kOriginBody[] = "origin";
constexpr char kProxyBody[] = "proxy";
constexpr char kExpectedProxyAuthorization[] = "Basic Zm9vOmJhcg==";
constexpr char kPrimaryGeoUrl[] = "http://freeipapi.test/geo-primary";
constexpr char kFallbackGeoUrl[] = "http://ipwhois.test/geo-fallback";
constexpr char kHttpsPrimaryGeoUrl[] = "https://freeipapi.test/geo-primary";
constexpr char kHttpsFallbackGeoUrl[] = "https://ipwhois.test/geo-fallback";
constexpr char kFreeIpApiAustralia[] = R"({
  "ipAddress":"1.1.1.1",
  "latitude":-33.8688,
  "longitude":151.2093,
  "countryName":"Australia",
  "countryCode":"AU",
  "timeZones":["Australia/Sydney"],
  "cityName":"Sydney",
  "regionName":"New South Wales"
})";
constexpr char kIpWhoIsUnitedStates[] = R"({
  "ip":"8.8.4.4",
  "success":true,
  "country":"United States",
  "country_code":"US",
  "region":"California",
  "city":"Mountain View",
  "latitude":37.3860517,
  "longitude":-122.0838511,
  "timezone":{"id":"America/Los_Angeles"}
})";
constexpr char kIpWhoIsAustralia[] = R"({
  "ip":"1.0.0.1",
  "success":true,
  "country":"Australia",
  "country_code":"AU",
  "region":"New South Wales",
  "city":"Sydney",
  "latitude":-33.8688,
  "longitude":151.2093,
  "timezone":{"id":"Australia/Sydney"}
})";

std::unique_ptr<net::test_server::BasicHttpResponse> TextResponse(
    std::string_view body) {
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/plain");
  response->set_content(body);
  return response;
}
using fingerprint_browser::test::LocalSocks5TestServer;

bool SettingsRuntimeErrorIsVisible(content::WebContents* web_contents,
                                   std::string_view expected_error) {
  constexpr char kScript[] = R"js(
    (async () => {
      for (let i = 0; i < 200; ++i) {
        const root = window.testing?.fingerprintProfileProxySubpage;
        const alerts = [...(root?.querySelectorAll('[role="alert"]') || [])];
        const errorRow = root?.getElementById('actionError');
        const text = [errorRow, ...alerts]
          .map(element => element?.innerText || '')
          .join(' ');
        if (text.includes($1)) {
          return true;
        }
        await new Promise(resolve => setTimeout(resolve, 25));
      }
      return false;
    })();
  )js";
  return content::EvalJs(web_contents,
                         content::JsReplace(kScript, expected_error))
      .ExtractBool();
}

bool SettingsNoProxyRiskMatches(content::WebContents* web_contents,
                                bool expected_visible) {
  constexpr char kScript[] = R"js(
    (async () => {
      for (let i = 0; i < 200; ++i) {
        const root = window.testing?.fingerprintProfileProxySubpage;
        const risk = root?.getElementById('noProxyRisk');
        const text = risk?.innerText || '';
        if (!root) {
          await new Promise(resolve => setTimeout(resolve, 25));
          continue;
        }

        if ($1) {
          if (risk &&
              risk.getAttribute('role') === 'alert' &&
              text.includes('真实网络出口')) {
            return true;
          }
        } else if (!risk || getComputedStyle(risk).display === 'none') {
          return true;
        }

        await new Promise(resolve => setTimeout(resolve, 25));
      }
      return false;
    })();
  )js";
  return content::EvalJs(web_contents,
                         content::JsReplace(kScript, expected_visible))
      .ExtractBool();
}

}  // namespace

class FingerprintBrowserProfileProxyBrowserTest
    : public InProcessBrowserTest,
      public content::TestDevToolsProtocolClient {
 public:
  FingerprintBrowserProfileProxyBrowserTest() {
    BraveSettingsUI::ShouldExposeElementsForTesting() = true;
  }
  ~FingerprintBrowserProfileProxyBrowserTest() override {
    BraveSettingsUI::ShouldExposeElementsForTesting() = false;
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    auto& config_map = content::WebUIConfigMap::GetInstance();
    config_map.RemoveConfig(GURL("chrome://settings"));
    config_map.AddWebUIConfig(std::make_unique<BraveSettingsUIConfig>());

    host_resolver()->AddRule("*", "127.0.0.1");

    origin_server_.RegisterRequestHandler(base::BindRepeating(
        &FingerprintBrowserProfileProxyBrowserTest::HandleOriginRequest,
        base::Unretained(this)));
    proxy_server_.RegisterRequestHandler(base::BindRepeating(
        &FingerprintBrowserProfileProxyBrowserTest::HandleProxyRequest,
        base::Unretained(this)));
    ASSERT_TRUE(origin_server_.Start());
    ASSERT_TRUE(proxy_server_.Start());
    fingerprint_browser::FingerprintProxyService::SetGeoProviderUrlsForTesting(
        GURL(kPrimaryGeoUrl), GURL(kFallbackGeoUrl));

#if BUILDFLAG(ENABLE_TOR)
    net::ProxyConfigServiceTor::SetBypassTorProxyConfigForTesting(true);
    tor::TorNavigationThrottle::SetSkipWaitForTorConnectedForTesting(true);
#endif
  }

  void TearDownOnMainThread() override {
    fingerprint_browser::FingerprintProxyService::
        ResetGeoProviderUrlsForTesting();
#if BUILDFLAG(ENABLE_TOR)
    tor::TorNavigationThrottle::SetSkipWaitForTorConnectedForTesting(false);
    net::ProxyConfigServiceTor::SetBypassTorProxyConfigForTesting(false);
#endif
    InProcessBrowserTest::TearDownOnMainThread();
  }

 protected:
  Profile* CreateTestProfile() {
    base::ScopedAllowBlockingForTesting allow_blocking;
    auto* profile_manager = g_browser_process->profile_manager();
    Profile* profile = profile_manager->GetProfile(
        profile_manager->GenerateNextProfileDirectoryPath());
    CHECK(profile);
    return profile;
  }

  void ConfigureProfileProxy(Profile* profile) {
    auto* service = GetProxyService(profile);
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return service->IsCredentialStoreReadyForTesting(); }));
    const auto verification = VerifyDraft(
        profile, {.scheme = fingerprint_browser::prefs::kProfileProxySchemeHttp,
                  .host = "127.0.0.1",
                  .port = ProxyPort(),
                  .username = "foo",
                  .password = "bar"});
    ASSERT_TRUE(verification.success) << verification.error_code;
    const auto apply = ApplyVerified(profile, verification.verification_id);
    ASSERT_TRUE(apply.success) << apply.error_code;
  }

  void ConfigureProfileProxyManualGeo(Profile* profile,
                                      std::string_view country_code,
                                      std::string_view timezone_id,
                                      double latitude,
                                      double longitude) {
    PrefService* prefs = profile->GetPrefs();
    prefs->SetBoolean(fingerprint_browser::prefs::kProfileProxyManualGeoEnabled,
                      true);
    prefs->SetString(
        fingerprint_browser::prefs::kProfileProxyManualGeoCountryCode,
        std::string(country_code));
    prefs->SetString(fingerprint_browser::prefs::kProfileProxyManualGeoTimezone,
                     std::string(timezone_id));
    prefs->SetDouble(fingerprint_browser::prefs::kProfileProxyManualGeoLatitude,
                     latitude);
    prefs->SetDouble(
        fingerprint_browser::prefs::kProfileProxyManualGeoLongitude, longitude);
    fingerprint_browser::SyncProfileProxyDerivedPrefs(*prefs);
  }

  fingerprint_browser::FingerprintProxyService* GetProxyService(
      Profile* profile) {
    return fingerprint_browser::FingerprintProxyServiceFactory::GetForProfile(
        profile);
  }

  fingerprint_browser::ProxyVerificationResult VerifyDraft(
      Profile* profile,
      fingerprint_browser::ProfileProxyDraft draft) {
    base::test::TestFuture<fingerprint_browser::ProxyVerificationResult> future;
    GetProxyService(profile)->VerifyDraft(std::move(draft),
                                          future.GetCallback());
    return future.Take();
  }

  fingerprint_browser::ProxyApplyResult ApplyVerified(
      Profile* profile,
      std::string verification_id) {
    base::test::TestFuture<fingerprint_browser::ProxyApplyResult> future;
    GetProxyService(profile)->ApplyVerified(std::move(verification_id),
                                            future.GetCallback());
    return future.Take();
  }

  fingerprint_browser::ProxyVerificationResult Revalidate(Profile* profile) {
    base::test::TestFuture<fingerprint_browser::ProxyVerificationResult> future;
    GetProxyService(profile)->Revalidate(future.GetCallback());
    return future.Take();
  }

  void SetGeoResponses(int primary_status,
                       std::string primary_body,
                       int fallback_status,
                       std::string fallback_body) {
    base::AutoLock lock(lock_);
    primary_geo_status_ = primary_status;
    primary_geo_body_ = std::move(primary_body);
    fallback_geo_status_ = fallback_status;
    fallback_geo_body_ = std::move(fallback_body);
  }

  void RequireProxyAuthorization(std::string authorization) {
    base::AutoLock lock(lock_);
    required_proxy_authorization_ = std::move(authorization);
  }

  void RejectProxyConnects(bool reject) {
    base::AutoLock lock(lock_);
    reject_proxy_connects_ = reject;
  }

  GURL OriginUrl(std::string_view host, std::string_view path) const {
    return origin_server_.GetURL(std::string(host), std::string(path));
  }

  std::string BodyText(Browser* browser) const {
    content::WebContents* web_contents =
        browser->tab_strip_model()->GetActiveWebContents();
    return content::EvalJs(web_contents, "document.body.innerText")
        .ExtractString();
  }

  int OriginRequestsForPath(std::string_view path) const {
    base::AutoLock lock(lock_);
    auto it = origin_requests_by_path_.find(std::string(path));
    return it == origin_requests_by_path_.end() ? 0 : it->second;
  }

  int ProxyChallengeCount() const {
    base::AutoLock lock(lock_);
    return proxy_challenge_count_;
  }

  bool SawExpectedProxyAuthorization() const {
    base::AutoLock lock(lock_);
    return saw_expected_proxy_authorization_;
  }

  bool ProxySawTargetContaining(std::string_view needle) const {
    base::AutoLock lock(lock_);
    for (const auto& target : proxy_request_targets_) {
      if (target.find(needle) != std::string::npos) {
        return true;
      }
    }
    return false;
  }

  size_t ProxyRequestCount() const {
    base::AutoLock lock(lock_);
    return proxy_request_targets_.size();
  }

  int ProxyPort() const { return proxy_server_.port(); }

 private:
  std::unique_ptr<net::test_server::HttpResponse> HandleOriginRequest(
      const net::test_server::HttpRequest& request) {
    if (request.relative_url != "/favicon.ico") {
      base::AutoLock lock(lock_);
      ++origin_requests_by_path_[request.relative_url];
    }
    return TextResponse(kOriginBody);
  }

  std::unique_ptr<net::test_server::HttpResponse> HandleProxyRequest(
      const net::test_server::HttpRequest& request) {
    {
      base::AutoLock lock(lock_);
      proxy_request_targets_.push_back(request.relative_url);
    }

    auto auth =
        request.headers.find(net::HttpRequestHeaders::kProxyAuthorization);
    base::AutoLock lock(lock_);
    if (auth == request.headers.end() ||
        auth->second != required_proxy_authorization_) {
      ++proxy_challenge_count_;
      auto response = TextResponse("proxy auth required");
      response->set_code(net::HTTP_PROXY_AUTHENTICATION_REQUIRED);
      response->AddCustomHeader("Proxy-Authenticate",
                                "Basic realm=\"profile-proxy\"");
      return response;
    }

    saw_expected_proxy_authorization_ = true;
    if (reject_proxy_connects_ &&
        request.method == net::test_server::METHOD_CONNECT) {
      auto response = TextResponse("tunnel unavailable");
      response->set_code(net::HTTP_BAD_GATEWAY);
      return response;
    }
    if (request.relative_url.find("/geo-primary") != std::string::npos) {
      auto response = TextResponse(primary_geo_body_);
      response->set_code(static_cast<net::HttpStatusCode>(primary_geo_status_));
      return response;
    }
    if (request.relative_url.find("/geo-fallback") != std::string::npos) {
      auto response = TextResponse(fallback_geo_body_);
      response->set_code(
          static_cast<net::HttpStatusCode>(fallback_geo_status_));
      return response;
    }
    return TextResponse(kProxyBody);
  }

  net::EmbeddedTestServer origin_server_;
  net::EmbeddedTestServer proxy_server_;

  mutable base::Lock lock_;
  std::map<std::string, int> origin_requests_by_path_;
  std::vector<std::string> proxy_request_targets_;
  int proxy_challenge_count_ GUARDED_BY(lock_) = 0;
  bool saw_expected_proxy_authorization_ GUARDED_BY(lock_) = false;
  bool reject_proxy_connects_ GUARDED_BY(lock_) = false;
  std::string required_proxy_authorization_ GUARDED_BY(lock_) =
      kExpectedProxyAuthorization;
  int primary_geo_status_ GUARDED_BY(lock_) = net::HTTP_OK;
  std::string primary_geo_body_ GUARDED_BY(lock_) = kFreeIpApiAustralia;
  int fallback_geo_status_ GUARDED_BY(lock_) = net::HTTP_OK;
  std::string fallback_geo_body_ GUARDED_BY(lock_) = kIpWhoIsUnitedStates;
};

IN_PROC_BROWSER_TEST_F(FingerprintBrowserProfileProxyBrowserTest,
                       ProfileProxyIsScopedAuthenticatedAndPersistent) {
  Profile* proxied_profile = CreateTestProfile();
  Profile* direct_profile = CreateTestProfile();
  ConfigureProfileProxy(proxied_profile);

  Browser* proxied_browser = CreateBrowser(proxied_profile);
  Browser* direct_browser = CreateBrowser(direct_profile);

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      proxied_browser, OriginUrl("profile-a.test", "/profile-a")));
  EXPECT_EQ(kProxyBody, BodyText(proxied_browser));
  EXPECT_EQ(0, OriginRequestsForPath("/profile-a"));
  EXPECT_GE(ProxyChallengeCount(), 1);
  EXPECT_TRUE(SawExpectedProxyAuthorization());

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      direct_browser, OriginUrl("profile-b.test", "/profile-b")));
  EXPECT_EQ(kOriginBody, BodyText(direct_browser));
  EXPECT_GE(OriginRequestsForPath("/profile-b"), 1);
  EXPECT_FALSE(ProxySawTargetContaining("/profile-b"));

  EXPECT_TRUE(proxied_profile->GetPrefs()->GetBoolean(
      fingerprint_browser::prefs::kProfileProxyEnabled));
  CloseBrowserSynchronously(proxied_browser);
  Browser* reopened_browser = CreateBrowser(proxied_profile);

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      reopened_browser, OriginUrl("profile-a.test", "/profile-a-reopened")));
  EXPECT_EQ(kProxyBody, BodyText(reopened_browser));
  EXPECT_EQ(0, OriginRequestsForPath("/profile-a-reopened"));
  EXPECT_TRUE(ProxySawTargetContaining("/profile-a-reopened"));
}

IN_PROC_BROWSER_TEST_F(FingerprintBrowserProfileProxyBrowserTest,
                       ProfileProxyAuthFailureWritesVisibleErrorState) {
  Profile* proxied_profile = CreateTestProfile();
  ConfigureProfileProxy(proxied_profile);
  const int challenge_count_before_failure = ProxyChallengeCount();
  RequireProxyAuthorization("Basic bmV3OnNlY3JldA==");

  Browser* proxied_browser = CreateBrowser(proxied_profile);
  EXPECT_TRUE(ui_test_utils::NavigateToURL(
      proxied_browser, OriginUrl("profile-a.test", "/bad-auth")));

  PrefService* prefs = proxied_profile->GetPrefs();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return prefs->GetInteger(
               fingerprint_browser::prefs::kProfileProxyLastErrorCode) ==
           net::ERR_INVALID_AUTH_CREDENTIALS;
  }));
  EXPECT_EQ(net::ERR_INVALID_AUTH_CREDENTIALS,
            prefs->GetInteger(
                fingerprint_browser::prefs::kProfileProxyLastErrorCode));
  EXPECT_EQ(
      "Proxy authentication failed. Check username/password.",
      prefs->GetString(fingerprint_browser::prefs::kProfileProxyLastError));
  EXPECT_GT(ProxyChallengeCount(), challenge_count_before_failure);
  EXPECT_EQ(0, OriginRequestsForPath("/bad-auth"));

  ASSERT_TRUE(ui_test_utils::NavigateToURL(proxied_browser,
                                           GURL("brave://settings/privacy")));
  EXPECT_TRUE(SettingsRuntimeErrorIsVisible(
      proxied_browser->tab_strip_model()->GetActiveWebContents(),
      "代理连接或认证失败。"));
}

IN_PROC_BROWSER_TEST_F(FingerprintBrowserProfileProxyBrowserTest,
                       PersonaWithoutProxyRiskWarningMatchesProxyState) {
  Profile* direct_profile = CreateTestProfile();
  Browser* direct_browser = CreateBrowser(direct_profile);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(direct_browser,
                                           GURL("brave://settings/privacy")));
  EXPECT_TRUE(SettingsNoProxyRiskMatches(
      direct_browser->tab_strip_model()->GetActiveWebContents(),
      /*expected_visible=*/true));

  Profile* proxied_profile = CreateTestProfile();
  ConfigureProfileProxy(proxied_profile);
  Browser* proxied_browser = CreateBrowser(proxied_profile);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(proxied_browser,
                                           GURL("brave://settings/privacy")));
  EXPECT_TRUE(SettingsNoProxyRiskMatches(
      proxied_browser->tab_strip_model()->GetActiveWebContents(),
      /*expected_visible=*/false));
}

IN_PROC_BROWSER_TEST_F(FingerprintBrowserProfileProxyBrowserTest,
                       GuestProfileKeepsProxySettingsUnavailable) {
  profiles::SwitchToGuestProfile();
  Browser* guest_browser = ui_test_utils::WaitForBrowserToOpen();
  ASSERT_TRUE(guest_browser);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      guest_browser, GURL("brave://settings/fingerprintProfileProxy")));
  EXPECT_FALSE(GetProxyService(guest_browser->profile()));
  EXPECT_EQ(guest_browser->tab_strip_model()
                ->GetActiveWebContents()
                ->GetLastCommittedURL(),
            GURL("chrome://settings/"));
}

IN_PROC_BROWSER_TEST_F(FingerprintBrowserProfileProxyBrowserTest,
                       VerificationFallsBackAppliesAndRevalidates) {
  Profile* profile = CreateTestProfile();
  auto* service = GetProxyService(profile);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return service->IsCredentialStoreReadyForTesting(); }));
  SetGeoResponses(net::HTTP_TOO_MANY_REQUESTS, "rate limited", net::HTTP_OK,
                  kIpWhoIsUnitedStates);

  fingerprint_browser::ProfileProxyDraft draft{
      .scheme = fingerprint_browser::prefs::kProfileProxySchemeHttp,
      .host = "127.0.0.1",
      .port = ProxyPort(),
      .username = "foo",
      .password = "bar"};
  const auto verification = VerifyDraft(profile, draft);
  ASSERT_TRUE(verification.success) << verification.error_code;
  EXPECT_EQ("ipwhois", verification.geo_provider);
  EXPECT_EQ("8.8.4.4", verification.egress_ip);
  ASSERT_TRUE(verification.geo);
  EXPECT_EQ("US", verification.geo->country_code);
  EXPECT_EQ(fingerprint_browser::kProxyStateAwaitingConfirmation,
            service->GetState().state);
  EXPECT_FALSE(profile->GetPrefs()->GetBoolean(
      fingerprint_browser::prefs::kProfileProxyEnabled));
  EXPECT_TRUE(ProxySawTargetContaining("/geo-primary"));
  EXPECT_TRUE(ProxySawTargetContaining("/geo-fallback"));

  const auto apply = ApplyVerified(profile, verification.verification_id);
  ASSERT_TRUE(apply.success) << apply.error_code;
  EXPECT_EQ(fingerprint_browser::kProxyStateActive, service->GetState().state);
  EXPECT_TRUE(profile->GetPrefs()->GetBoolean(
      fingerprint_browser::prefs::kProfileProxyEnabled));
  EXPECT_TRUE(profile->GetPrefs()
                  ->GetString(fingerprint_browser::prefs::kProfileProxyPassword)
                  .empty());
  EXPECT_FALSE(
      profile->GetPrefs()
          ->GetString(
              fingerprint_browser::prefs::kProfileProxyEncryptedPassword)
          .empty());
  EXPECT_EQ("en-US,en",
            profile->GetPrefs()->GetString("intl.accept_languages"));
  EXPECT_EQ("disable_non_proxied_udp",
            profile->GetPrefs()->GetString("webrtc.ip_handling_policy"));
  EXPECT_EQ("America/Los_Angeles",
            profile->GetPrefs()->GetString(
                fingerprint_browser::prefs::kProfileProxyDerivedGeoTimezone));

  const auto duplicate = ApplyVerified(profile, verification.verification_id);
  EXPECT_FALSE(duplicate.success);

  Browser* proxied_browser = CreateBrowser(profile);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      proxied_browser, OriginUrl("profile-verified.test", "/verified")));
  EXPECT_EQ(kProxyBody, BodyText(proxied_browser));
  EXPECT_EQ(0, OriginRequestsForPath("/verified"));

  SetGeoResponses(net::HTTP_TOO_MANY_REQUESTS, "rate limited", net::HTTP_OK,
                  kIpWhoIsAustralia);
  const auto revalidation = Revalidate(profile);
  ASSERT_TRUE(revalidation.success) << revalidation.error_code;
  const auto state = service->GetState();
  ASSERT_TRUE(state.geo);
  EXPECT_EQ("AU", state.geo->country_code);
  EXPECT_EQ("1.0.0.1", state.egress_ip);
  EXPECT_NE(state.warning_code, fingerprint_browser::kProxyWarningNone);

  const auto stable_revalidation = Revalidate(profile);
  ASSERT_TRUE(stable_revalidation.success) << stable_revalidation.error_code;
  EXPECT_EQ(state.warning_code, service->GetState().warning_code);
}

IN_PROC_BROWSER_TEST_F(FingerprintBrowserProfileProxyBrowserTest,
                       VerificationFailureNeverEnablesProxy) {
  Profile* profile = CreateTestProfile();
  SetGeoResponses(net::HTTP_INTERNAL_SERVER_ERROR, "primary failed",
                  net::HTTP_SERVICE_UNAVAILABLE, "fallback failed");

  const auto verification = VerifyDraft(
      profile, {.scheme = fingerprint_browser::prefs::kProfileProxySchemeHttp,
                .host = "127.0.0.1",
                .port = ProxyPort(),
                .username = "foo",
                .password = "bar"});
  EXPECT_FALSE(verification.success);
  EXPECT_FALSE(profile->GetPrefs()->GetBoolean(
      fingerprint_browser::prefs::kProfileProxyEnabled));
  EXPECT_EQ(fingerprint_browser::kProxyStateError,
            GetProxyService(profile)->GetState().state);
  EXPECT_TRUE(ProxySawTargetContaining("/geo-primary"));
  EXPECT_TRUE(ProxySawTargetContaining("/geo-fallback"));
}

IN_PROC_BROWSER_TEST_F(
    FingerprintBrowserProfileProxyBrowserTest,
    TransientTunnelFailureKeepsLastKnownStateAndRetriesSoon) {
  Profile* profile = CreateTestProfile();
  ConfigureProfileProxy(profile);
  auto* service = GetProxyService(profile);
  const auto healthy_state = service->GetState();
  ASSERT_EQ(fingerprint_browser::kProxyStateActive, healthy_state.state);
  ASSERT_TRUE(healthy_state.geo);

  fingerprint_browser::FingerprintProxyService::SetGeoProviderUrlsForTesting(
      GURL(kHttpsPrimaryGeoUrl), GURL(kHttpsFallbackGeoUrl));
  RejectProxyConnects(true);
  const auto revalidation = Revalidate(profile);

  ASSERT_FALSE(revalidation.success);
  EXPECT_EQ(net::ERR_TUNNEL_CONNECTION_FAILED, revalidation.net_error);
  const auto degraded_state = service->GetState();
  EXPECT_EQ(fingerprint_browser::kProxyStateStale, degraded_state.state);
  EXPECT_EQ(fingerprint_browser::kProxyMessageRevalidationRetrying,
            degraded_state.status_code);
  EXPECT_TRUE(degraded_state.enabled);
  EXPECT_EQ(healthy_state.egress_ip, degraded_state.egress_ip);
  ASSERT_TRUE(degraded_state.geo);
  EXPECT_EQ(healthy_state.geo->country_code, degraded_state.geo->country_code);
  EXPECT_LE(service->RevalidationDelayForTesting(), base::Seconds(30));

  Browser* proxied_browser = CreateBrowser(profile);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      proxied_browser, OriginUrl("still-usable.test", "/after-tunnel-error")));
  EXPECT_EQ(kProxyBody, BodyText(proxied_browser));
  EXPECT_EQ(0, OriginRequestsForPath("/after-tunnel-error"));

  RejectProxyConnects(false);
  fingerprint_browser::FingerprintProxyService::SetGeoProviderUrlsForTesting(
      GURL(kPrimaryGeoUrl), GURL(kFallbackGeoUrl));
  service->FireRevalidationTimerForTesting();
  EXPECT_TRUE(base::test::RunUntil([&] {
    return service->GetState().state == fingerprint_browser::kProxyStateActive;
  }));
}

IN_PROC_BROWSER_TEST_F(
    FingerprintBrowserProfileProxyBrowserTest,
    RepeatedTunnelFailuresEscalateAndSuccessfulRetryResetsBackoff) {
  Profile* profile = CreateTestProfile();
  ConfigureProfileProxy(profile);
  auto* service = GetProxyService(profile);

  fingerprint_browser::FingerprintProxyService::SetGeoProviderUrlsForTesting(
      GURL(kHttpsPrimaryGeoUrl), GURL(kHttpsFallbackGeoUrl));
  RejectProxyConnects(true);

  EXPECT_FALSE(Revalidate(profile).success);
  EXPECT_EQ(fingerprint_browser::kProxyStateStale, service->GetState().state);
  EXPECT_FALSE(Revalidate(profile).success);
  EXPECT_EQ(fingerprint_browser::kProxyStateStale, service->GetState().state);
  EXPECT_FALSE(Revalidate(profile).success);
  EXPECT_EQ(fingerprint_browser::kProxyStateError, service->GetState().state);

  RejectProxyConnects(false);
  fingerprint_browser::FingerprintProxyService::SetGeoProviderUrlsForTesting(
      GURL(kPrimaryGeoUrl), GURL(kFallbackGeoUrl));
  EXPECT_TRUE(Revalidate(profile).success);
  EXPECT_EQ(fingerprint_browser::kProxyStateActive, service->GetState().state);
  EXPECT_GE(service->RevalidationDelayForTesting(), base::Minutes(10));

  fingerprint_browser::FingerprintProxyService::SetGeoProviderUrlsForTesting(
      GURL(kHttpsPrimaryGeoUrl), GURL(kHttpsFallbackGeoUrl));
  RejectProxyConnects(true);
  EXPECT_FALSE(Revalidate(profile).success);
  EXPECT_EQ(fingerprint_browser::kProxyStateStale, service->GetState().state);
  EXPECT_LE(service->RevalidationDelayForTesting(), base::Seconds(30));
}

IN_PROC_BROWSER_TEST_F(FingerprintBrowserProfileProxyBrowserTest,
                       Socks5NoAuthVerifiesUsesProxyDnsAndFailsClosed) {
  LocalSocks5TestServer socks5_server("", "");
  ASSERT_TRUE(socks5_server.Start());
  Profile* profile = CreateTestProfile();
  auto* service = GetProxyService(profile);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return service->IsCredentialStoreReadyForTesting(); }));

  const auto verification = VerifyDraft(
      profile, {.scheme = fingerprint_browser::prefs::kProfileProxySchemeSocks5,
                .host = "127.0.0.1",
                .port = socks5_server.port()});

  ASSERT_TRUE(verification.success) << verification.error_code;
  EXPECT_TRUE(socks5_server.SawTargetHost("freeipapi.test"));
  const auto apply = ApplyVerified(profile, verification.verification_id);
  ASSERT_TRUE(apply.success) << apply.error_code;

  Browser* proxied_browser = CreateBrowser(profile);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      proxied_browser, OriginUrl("socks-target.test", "/socks-no-auth")));
  EXPECT_EQ(kProxyBody, BodyText(proxied_browser));
  EXPECT_TRUE(socks5_server.SawTargetHost("socks-target.test"));
  EXPECT_EQ(0, OriginRequestsForPath("/socks-no-auth"));

  socks5_server.Stop();
  EXPECT_TRUE(ui_test_utils::NavigateToURL(
      proxied_browser,
      OriginUrl("socks-target.test", "/socks-disconnected")));
  PrefService* prefs = profile->GetPrefs();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return GetProxyService(profile)->GetState().state ==
           fingerprint_browser::kProxyStateError;
  }));
  const int net_error =
      prefs->GetInteger(fingerprint_browser::prefs::kProfileProxyLastErrorCode);
  EXPECT_TRUE(net_error == net::ERR_PROXY_CONNECTION_FAILED ||
              net_error == net::ERR_SOCKS_CONNECTION_FAILED);
  EXPECT_EQ(0, OriginRequestsForPath("/socks-disconnected"));
}

IN_PROC_BROWSER_TEST_F(FingerprintBrowserProfileProxyBrowserTest,
                       Socks5UsernamePasswordAuthenticatesWithoutPrompt) {
  LocalSocks5TestServer socks5_server("foo", "bar");
  ASSERT_TRUE(socks5_server.Start());
  Profile* profile = CreateTestProfile();
  auto* service = GetProxyService(profile);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return service->IsCredentialStoreReadyForTesting(); }));

  const auto verification = VerifyDraft(
      profile, {.scheme = fingerprint_browser::prefs::kProfileProxySchemeSocks5,
                .host = "127.0.0.1",
                .port = socks5_server.port(),
                .username = "foo",
                .password = "bar"});

  ASSERT_TRUE(verification.success) << verification.error_code;
  EXPECT_TRUE(socks5_server.SawTargetHost("freeipapi.test"));
  EXPECT_GE(socks5_server.successful_authentications(), 1);
  const auto apply = ApplyVerified(profile, verification.verification_id);
  ASSERT_TRUE(apply.success) << apply.error_code;

  Browser* proxied_browser = CreateBrowser(profile);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      proxied_browser, OriginUrl("socks-auth.test", "/socks-auth")));
  EXPECT_EQ(kProxyBody, BodyText(proxied_browser));
  EXPECT_TRUE(socks5_server.SawTargetHost("socks-auth.test"));
  EXPECT_GE(socks5_server.successful_authentications(), 2);
  EXPECT_EQ(0, OriginRequestsForPath("/socks-auth"));
}

IN_PROC_BROWSER_TEST_F(FingerprintBrowserProfileProxyBrowserTest,
                       Socks5AuthenticationFailureDoesNotEnableProxy) {
  LocalSocks5TestServer socks5_server("expected", "secret");
  ASSERT_TRUE(socks5_server.Start());
  Profile* profile = CreateTestProfile();

  const auto verification = VerifyDraft(
      profile, {.scheme = fingerprint_browser::prefs::kProfileProxySchemeSocks5,
                .host = "127.0.0.1",
                .port = socks5_server.port(),
                .username = "foo",
                .password = "bar"});

  EXPECT_FALSE(verification.success);
  EXPECT_EQ(fingerprint_browser::kProxyMessageConnectionFailed,
            verification.error_code)
      << verification.net_error;
  EXPECT_GE(socks5_server.rejected_authentications(), 1);
  EXPECT_FALSE(profile->GetPrefs()->GetBoolean(
      fingerprint_browser::prefs::kProfileProxyEnabled));
}

IN_PROC_BROWSER_TEST_F(FingerprintBrowserProfileProxyBrowserTest,
                       SavedSocks5ConfigurationRevalidatesAfterServiceRestart) {
  LocalSocks5TestServer socks5_server("foo", "bar");
  ASSERT_TRUE(socks5_server.Start());
  Profile* profile = CreateTestProfile();
  auto* service = GetProxyService(profile);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return service->IsCredentialStoreReadyForTesting(); }));
  const auto verification = VerifyDraft(
      profile, {.scheme = fingerprint_browser::prefs::kProfileProxySchemeSocks5,
                .host = "127.0.0.1",
                .port = socks5_server.port(),
                .username = "foo",
                .password = "bar"});
  ASSERT_TRUE(verification.success) << verification.error_code;
  ASSERT_TRUE(ApplyVerified(profile, verification.verification_id).success);
  const int authentications_before_reload =
      socks5_server.successful_authentications();
  service = static_cast<fingerprint_browser::FingerprintProxyService*>(
      fingerprint_browser::FingerprintProxyServiceFactory::GetInstance()
          ->SetTestingFactoryAndUse(
              profile,
              base::BindOnce(
                  [](content::BrowserContext* context)
                      -> std::unique_ptr<KeyedService> {
                    return std::make_unique<
                        fingerprint_browser::FingerprintProxyService>(
                        Profile::FromBrowserContext(context));
                  })));
  ASSERT_TRUE(service);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return service->IsCredentialStoreReadyForTesting(); }));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return service->GetState().state == fingerprint_browser::kProxyStateActive;
  }));
  EXPECT_TRUE(socks5_server.SawTargetHost("freeipapi.test"));
  EXPECT_GT(socks5_server.successful_authentications(),
            authentications_before_reload);

  const auto proxy = service->GetProxyServer();
  ASSERT_TRUE(proxy);
  EXPECT_EQ(net::ProxyServer::SCHEME_SOCKS5, proxy->scheme());
  EXPECT_EQ("127.0.0.1", proxy->host_port_pair().host());
  EXPECT_EQ(socks5_server.port(), proxy->host_port_pair().port());
}

IN_PROC_BROWSER_TEST_F(FingerprintBrowserProfileProxyBrowserTest,
                       WebUiSavesAndReloadsSocks5Selection) {
  LocalSocks5TestServer socks5_server("", "");
  ASSERT_TRUE(socks5_server.Start());
  Profile* profile = CreateTestProfile();
  Browser* settings_browser = CreateBrowser(profile);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      settings_browser, GURL("brave://settings/fingerprintProfileProxy")));
  auto* web_contents =
      settings_browser->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(web_contents);

  constexpr char kConfigureScript[] = R"js(
    (async () => {
      for (let attempt = 0; attempt < 200; ++attempt) {
        const root = window.testing?.fingerprintProfileProxySubpage;
        const scheme = root?.getElementById('scheme');
        const verify = root?.getElementById('verify');
        if (scheme && verify) {
          scheme.value = 'socks5';
          scheme.dispatchEvent(new Event('change', {bubbles: true}));
          for (const [id, value] of [
            ['host', '127.0.0.1'], ['port', String($1)]
          ]) {
            const input = root.getElementById(id);
            input.value = value;
            input.dispatchEvent(new Event('input', {bubbles: true}));
          }
          verify.click();
          for (let resultAttempt = 0; resultAttempt < 200; ++resultAttempt) {
            const result = root.getElementById('verificationResult');
            if (result) {
              result.querySelector('.action-button').click();
              break;
            }
            const error = root.getElementById('actionError');
            if (error) {
              return `verify-error:${error.innerText}`;
            }
            await new Promise(resolve => setTimeout(resolve, 25));
          }
          for (let applyAttempt = 0; applyAttempt < 200; ++applyAttempt) {
            if (root.getElementById('activeProxy')) {
              return scheme.value;
            }
            const error = root.getElementById('actionError');
            if (error) {
              return `apply-error:${error.innerText}`;
            }
            await new Promise(resolve => setTimeout(resolve, 25));
          }
          return 'apply-timeout';
        }
        await new Promise(resolve => setTimeout(resolve, 25));
      }
      return 'missing';
    })()
  )js";
  EXPECT_EQ(
      content::EvalJs(web_contents, content::JsReplace(kConfigureScript,
                                                       socks5_server.port())),
      "socks5");
  EXPECT_EQ(fingerprint_browser::prefs::kProfileProxySchemeSocks5,
            profile->GetPrefs()->GetString(
                fingerprint_browser::prefs::kProfileProxyScheme));
  EXPECT_EQ(fingerprint_browser::kProxyStateActive,
            GetProxyService(profile)->GetState().state);

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      settings_browser, GURL("brave://settings/fingerprintProfileProxy")));
  EXPECT_EQ(content::EvalJs(web_contents, R"js(
    (async () => {
      for (let attempt = 0; attempt < 200; ++attempt) {
        const root = window.testing?.fingerprintProfileProxySubpage;
        const scheme = root?.getElementById('scheme');
        if (scheme) {
          return scheme.value;
        }
        await new Promise(resolve => setTimeout(resolve, 25));
      }
      return 'missing';
    })()
  )js"),
            "socks5");
}

IN_PROC_BROWSER_TEST_F(FingerprintBrowserProfileProxyBrowserTest,
                       BadAuthenticationAndExpiredTokenAreRejected) {
  Profile* bad_auth_profile = CreateTestProfile();
  const auto bad_auth = VerifyDraft(
      bad_auth_profile,
      {.scheme = fingerprint_browser::prefs::kProfileProxySchemeHttp,
       .host = "127.0.0.1",
       .port = ProxyPort(),
       .username = "wrong",
       .password = "credentials"});
  EXPECT_FALSE(bad_auth.success);
  EXPECT_FALSE(bad_auth_profile->GetPrefs()->GetBoolean(
      fingerprint_browser::prefs::kProfileProxyEnabled));

  Profile* expired_profile = CreateTestProfile();
  const auto verification = VerifyDraft(
      expired_profile,
      {.scheme = fingerprint_browser::prefs::kProfileProxySchemeHttp,
       .host = "127.0.0.1",
       .port = ProxyPort(),
       .username = "foo",
       .password = "bar"});
  ASSERT_TRUE(verification.success) << verification.error_code;
  GetProxyService(expired_profile)->ExpirePendingVerificationForTesting();
  const auto apply =
      ApplyVerified(expired_profile, verification.verification_id);
  EXPECT_FALSE(apply.success);
  EXPECT_FALSE(expired_profile->GetPrefs()->GetBoolean(
      fingerprint_browser::prefs::kProfileProxyEnabled));
}

IN_PROC_BROWSER_TEST_F(FingerprintBrowserProfileProxyBrowserTest,
                       ChangedDraftInvalidatesOldVerificationToken) {
  Profile* profile = CreateTestProfile();
  const auto first = VerifyDraft(
      profile, {.scheme = fingerprint_browser::prefs::kProfileProxySchemeHttp,
                .host = "127.0.0.1",
                .port = ProxyPort(),
                .username = "foo",
                .password = "bar"});
  ASSERT_TRUE(first.success) << first.error_code;

  const auto second = VerifyDraft(
      profile, {.scheme = fingerprint_browser::prefs::kProfileProxySchemeHttp,
                .host = "localhost",
                .port = ProxyPort(),
                .username = "foo",
                .password = "bar"});
  ASSERT_TRUE(second.success) << second.error_code;
  EXPECT_FALSE(ApplyVerified(profile, first.verification_id).success);
  EXPECT_TRUE(ApplyVerified(profile, second.verification_id).success);
  EXPECT_EQ("localhost", GetProxyService(profile)->GetState().host);
}

IN_PROC_BROWSER_TEST_F(FingerprintBrowserProfileProxyBrowserTest,
                       SavedPasswordIsReusedOnlyForSameProxyIdentity) {
  Profile* profile = CreateTestProfile();
  ConfigureProfileProxy(profile);
  const size_t requests_before_changed_draft = ProxyRequestCount();

  const auto changed_proxy = VerifyDraft(
      profile, {.scheme = fingerprint_browser::prefs::kProfileProxySchemeHttp,
                .host = "localhost",
                .port = ProxyPort(),
                .username = "foo"});
  EXPECT_FALSE(changed_proxy.success);
  EXPECT_EQ(fingerprint_browser::kProxyMessagePasswordRequired,
            changed_proxy.error_code);
  EXPECT_EQ(requests_before_changed_draft, ProxyRequestCount());

  const auto same_proxy = VerifyDraft(
      profile, {.scheme = fingerprint_browser::prefs::kProfileProxySchemeHttp,
                .host = "127.0.0.1",
                .port = ProxyPort(),
                .username = "foo"});
  EXPECT_TRUE(same_proxy.success) << same_proxy.error_code;
}

IN_PROC_BROWSER_TEST_F(FingerprintBrowserProfileProxyBrowserTest,
                       FailedDraftVerificationPreservesActiveProxy) {
  Profile* profile = CreateTestProfile();
  ConfigureProfileProxy(profile);
  const auto before = GetProxyService(profile)->GetState();
  ASSERT_EQ(fingerprint_browser::kProxyStateActive, before.state);

  SetGeoResponses(net::HTTP_INTERNAL_SERVER_ERROR, "primary failed",
                  net::HTTP_SERVICE_UNAVAILABLE, "fallback failed");
  const auto failed = VerifyDraft(
      profile, {.scheme = fingerprint_browser::prefs::kProfileProxySchemeHttp,
                .host = "localhost",
                .port = ProxyPort(),
                .username = "foo",
                .password = "bar"});
  EXPECT_FALSE(failed.success);

  const auto after = GetProxyService(profile)->GetState();
  EXPECT_EQ(before.state, after.state);
  EXPECT_EQ(before.status_code, after.status_code);
  EXPECT_EQ(before.warning_code, after.warning_code);
  EXPECT_EQ(before.egress_ip, after.egress_ip);
  EXPECT_EQ(before.host, after.host);
  EXPECT_TRUE(after.enabled);

  Browser* browser = CreateBrowser(profile);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser, OriginUrl("preserved.test", "/preserved")));
  EXPECT_EQ(kProxyBody, BodyText(browser));
}

IN_PROC_BROWSER_TEST_F(FingerprintBrowserProfileProxyBrowserTest,
                       PasswordMigrationAndDecryptionFailureFailClosed) {
  Profile* migration_profile = CreateTestProfile();
  auto* migration_service = GetProxyService(migration_profile);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return migration_service->IsCredentialStoreReadyForTesting(); }));
  PrefService* migration_prefs = migration_profile->GetPrefs();
  migration_prefs->ClearPref(
      fingerprint_browser::prefs::kProfileProxyEncryptedPassword);
  migration_prefs->SetString(fingerprint_browser::prefs::kProfileProxyPassword,
                             "legacy-secret");
  ASSERT_TRUE(migration_service->MigratePlaintextPasswordForTesting());
  EXPECT_TRUE(migration_prefs
                  ->GetString(fingerprint_browser::prefs::kProfileProxyPassword)
                  .empty());
  EXPECT_FALSE(
      migration_prefs
          ->GetString(
              fingerprint_browser::prefs::kProfileProxyEncryptedPassword)
          .empty());

  Profile* blocked_profile = CreateTestProfile();
  ConfigureProfileProxy(blocked_profile);
  auto* blocked_service = GetProxyService(blocked_profile);
  PrefService* blocked_prefs = blocked_profile->GetPrefs();
  blocked_prefs->SetString(
      fingerprint_browser::prefs::kProfileProxyEncryptedPassword,
      "not-valid-encrypted-data");
  blocked_prefs->SetInteger(
      fingerprint_browser::prefs::kProfileProxyCredentialGeneration,
      blocked_prefs->GetInteger(
          fingerprint_browser::prefs::kProfileProxyCredentialGeneration) +
          1);

  const auto blocking_proxy = blocked_service->GetProxyServer();
  ASSERT_TRUE(blocking_proxy);
  EXPECT_EQ(9, blocking_proxy->host_port_pair().port());
  const size_t proxy_requests_before = ProxyRequestCount();

  Browser* blocked_browser = CreateBrowser(blocked_profile);
  EXPECT_TRUE(ui_test_utils::NavigateToURL(
      blocked_browser, OriginUrl("blocked.test", "/blocked")));
  EXPECT_NE(std::string::npos,
            BodyText(blocked_browser).find("ERR_PROXY_CONNECTION_FAILED"));
  EXPECT_EQ(proxy_requests_before, ProxyRequestCount());
}

IN_PROC_BROWSER_TEST_F(FingerprintBrowserProfileProxyBrowserTest,
                       ConcurrentProfilesKeepIdentityStateIsolated) {
  Profile* proxied_profile = CreateTestProfile();
  Profile* direct_profile = CreateTestProfile();
  ConfigureProfileProxy(proxied_profile);

  const auto* proxied_persona =
      fingerprint_browser::GetPersonaForProfile(proxied_profile);
  const auto* direct_persona =
      fingerprint_browser::GetPersonaForProfile(direct_profile);
  ASSERT_TRUE(proxied_persona);
  ASSERT_TRUE(direct_persona);
  EXPECT_NE(proxied_persona->persona_id, direct_persona->persona_id);

  Browser* proxied_browser = CreateBrowser(proxied_profile);
  Browser* direct_browser = CreateBrowser(direct_profile);
  const GURL shared_url = OriginUrl("identity.test", "/identity");

  ASSERT_TRUE(ui_test_utils::NavigateToURL(proxied_browser, shared_url));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(direct_browser, shared_url));
  EXPECT_EQ(kProxyBody, BodyText(proxied_browser));
  EXPECT_EQ(kOriginBody, BodyText(direct_browser));
  EXPECT_TRUE(ProxySawTargetContaining("/identity"));
  EXPECT_GE(OriginRequestsForPath("/identity"), 1);

  content::WebContents* proxied_contents =
      proxied_browser->tab_strip_model()->GetActiveWebContents();
  content::WebContents* direct_contents =
      direct_browser->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ(
      proxied_persona->user_agent,
      content::EvalJs(proxied_contents, "navigator.userAgent").ExtractString());
  EXPECT_EQ(
      direct_persona->user_agent,
      content::EvalJs(direct_contents, "navigator.userAgent").ExtractString());

  ASSERT_TRUE(
      content::ExecJs(proxied_contents,
                      "localStorage.setItem('identity', 'profile-a');"
                      "document.cookie = 'identity=profile-a; path=/';"));
  ASSERT_TRUE(
      content::ExecJs(direct_contents,
                      "localStorage.setItem('identity', 'profile-b');"
                      "document.cookie = 'identity=profile-b; path=/';"));
  EXPECT_EQ("profile-a", content::EvalJs(proxied_contents,
                                         "localStorage.getItem('identity')")
                             .ExtractString());
  EXPECT_EQ("profile-b",
            content::EvalJs(direct_contents, "localStorage.getItem('identity')")
                .ExtractString());
  EXPECT_NE(std::string::npos,
            content::EvalJs(proxied_contents, "document.cookie")
                .ExtractString()
                .find("identity=profile-a"));
  EXPECT_EQ(std::string::npos,
            content::EvalJs(proxied_contents, "document.cookie")
                .ExtractString()
                .find("identity=profile-b"));
  EXPECT_NE(std::string::npos,
            content::EvalJs(direct_contents, "document.cookie")
                .ExtractString()
                .find("identity=profile-b"));
  EXPECT_EQ(std::string::npos,
            content::EvalJs(direct_contents, "document.cookie")
                .ExtractString()
                .find("identity=profile-a"));

  base::FilePath proxied_profile_path = proxied_profile->GetPath();
  CloseBrowserSynchronously(proxied_browser);

  ProfileDeletionObserver deletion_observer;
  g_browser_process->profile_manager()
      ->GetDeleteProfileHelper()
      .MaybeScheduleProfileForDeletion(
          proxied_profile_path, base::DoNothing(),
          ProfileMetrics::DELETE_PROFILE_USER_MANAGER);
  deletion_observer.Wait();

  EXPECT_EQ(nullptr, g_browser_process->profile_manager()
                         ->GetProfileAttributesStorage()
                         .GetProfileAttributesWithPath(proxied_profile_path));
}

IN_PROC_BROWSER_TEST_F(FingerprintBrowserProfileProxyBrowserTest,
                       IncognitoInheritsProfileProxyServiceAndRouting) {
  Profile* profile = CreateTestProfile();
  ConfigureProfileProxy(profile);
  auto* regular_service = GetProxyService(profile);

  Browser* incognito_browser = CreateIncognitoBrowser(profile);
  ASSERT_TRUE(incognito_browser);
  const auto* regular_persona =
      fingerprint_browser::GetPersonaForProfile(profile);
  const auto* incognito_persona =
      fingerprint_browser::GetPersonaForProfile(incognito_browser->profile());
  ASSERT_TRUE(regular_persona);
  ASSERT_TRUE(incognito_persona);
  EXPECT_EQ(regular_persona->persona_id, incognito_persona->persona_id);
  EXPECT_EQ(regular_service, GetProxyService(incognito_browser->profile()));
  EXPECT_EQ(
      regular_service->GetState().egress_ip,
      GetProxyService(incognito_browser->profile())->GetState().egress_ip);

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      incognito_browser, OriginUrl("incognito.test", "/incognito")));
  EXPECT_EQ(kProxyBody, BodyText(incognito_browser));
  EXPECT_TRUE(ProxySawTargetContaining("/incognito"));
  EXPECT_EQ(0, OriginRequestsForPath("/incognito"));

  const auto changed = VerifyDraft(
      profile, {.scheme = fingerprint_browser::prefs::kProfileProxySchemeHttp,
                .host = "localhost",
                .port = ProxyPort(),
                .username = "foo",
                .password = "bar"});
  ASSERT_TRUE(changed.success) << changed.error_code;
  ASSERT_TRUE(ApplyVerified(profile, changed.verification_id).success);
  EXPECT_EQ("localhost",
            GetProxyService(incognito_browser->profile())->GetState().host);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      incognito_browser, OriginUrl("incognito.test", "/incognito-switched")));
  EXPECT_EQ(kProxyBody, BodyText(incognito_browser));
  EXPECT_EQ(0, OriginRequestsForPath("/incognito-switched"));

  base::RunLoop disabled;
  regular_service->Disable(disabled.QuitClosure());
  disabled.Run();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      incognito_browser, OriginUrl("incognito.test", "/incognito-disabled")));
  EXPECT_EQ(kOriginBody, BodyText(incognito_browser));
  EXPECT_GE(OriginRequestsForPath("/incognito-disabled"), 1);
}

IN_PROC_BROWSER_TEST_F(FingerprintBrowserProfileProxyBrowserTest,
                       ProfileProxyGeolocationUsesProfileGeo) {
  Profile* proxied_profile = CreateTestProfile();
  ConfigureProfileProxy(proxied_profile);
  ConfigureProfileProxyManualGeo(proxied_profile, "GB", "Europe/London",
                                 51.5074, -0.1278);

  Browser* proxied_browser = CreateBrowser(proxied_profile);
  const GURL geolocation_url = OriginUrl("localhost", "/profile-geo");
  HostContentSettingsMapFactory::GetForProfile(proxied_profile)
      ->SetContentSettingDefaultScope(geolocation_url, geolocation_url,
                                      ContentSettingsType::GEOLOCATION,
                                      CONTENT_SETTING_ALLOW);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(proxied_browser, geolocation_url));
  content::WebContents* web_contents =
      proxied_browser->tab_strip_model()->GetActiveWebContents();

  constexpr char kGetPosition[] = R"js(
    new Promise(resolve => {
      navigator.geolocation.getCurrentPosition(
          position => resolve(`${position.coords.latitude},${position.coords.longitude}`),
          error => resolve(`error:${error.code}`));
    })
  )js";
  EXPECT_EQ("51.5074,-0.1278",
            content::EvalJs(web_contents, kGetPosition).ExtractString());
}

IN_PROC_BROWSER_TEST_F(FingerprintBrowserProfileProxyBrowserTest,
                       ProfileProxyTimezoneUsesProfileGeo) {
  Profile* proxied_profile = CreateTestProfile();
  ConfigureProfileProxy(proxied_profile);
  ConfigureProfileProxyManualGeo(proxied_profile, "GB", "Europe/London",
                                 51.5074, -0.1278);

  Browser* proxied_browser = CreateBrowser(proxied_profile);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      proxied_browser, OriginUrl("localhost", "/profile-timezone")));
  content::WebContents* web_contents =
      proxied_browser->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ("Europe/London", web_contents->GetMutableRendererPrefs()
                                 ->fingerprint_browser_timezone_override);
  EXPECT_EQ("Europe/London",
            content::EvalJs(web_contents,
                            "Intl.DateTimeFormat().resolvedOptions().timeZone")
                .ExtractString());
  EXPECT_EQ(
      0, content::EvalJs(web_contents,
                         "new Date('2026-01-15T12:00:00Z').getTimezoneOffset()")
             .ExtractInt());
}

IN_PROC_BROWSER_TEST_F(FingerprintBrowserProfileProxyBrowserTest,
                       ProfileProxyTimezoneUpdatesWithoutRendererRestart) {
  Profile* proxied_profile = CreateTestProfile();
  ConfigureProfileProxy(proxied_profile);
  ConfigureProfileProxyManualGeo(proxied_profile, "GB", "Europe/London",
                                 51.5074, -0.1278);

  Browser* proxied_browser = CreateBrowser(proxied_profile);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      proxied_browser, OriginUrl("localhost", "/profile-timezone-update")));
  content::WebContents* web_contents =
      proxied_browser->tab_strip_model()->GetActiveWebContents();
  constexpr char kTimezoneExpression[] =
      "Intl.DateTimeFormat().resolvedOptions().timeZone";
  EXPECT_EQ("Europe/London",
            content::EvalJs(web_contents, kTimezoneExpression).ExtractString());

  ConfigureProfileProxyManualGeo(proxied_profile, "AU", "Australia/Sydney",
                                 -33.8688, 151.2093);
  content::WebContents::SyncRendererPrefsForBrowserContext(proxied_profile);
  EXPECT_EQ("Australia/Sydney", web_contents->GetMutableRendererPrefs()
                                    ->fingerprint_browser_timezone_override);
  EXPECT_TRUE(web_contents->GetMutableRendererPrefs()
                  ->fingerprint_browser_timezone_override_initialized);
  EXPECT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(web_contents, kTimezoneExpression).ExtractString() ==
           "Australia/Sydney";
  }));

  EXPECT_EQ("Australia/Sydney",
            content::EvalJs(web_contents, kTimezoneExpression).ExtractString());
  EXPECT_EQ(-660, content::EvalJs(
                      web_contents,
                      "new Date('2026-01-15T12:00:00Z').getTimezoneOffset()")
                      .ExtractInt());
}

IN_PROC_BROWSER_TEST_F(FingerprintBrowserProfileProxyBrowserTest,
                       DevToolsTimezoneTemporarilyOverridesProfileTimezone) {
  Profile* proxied_profile = CreateTestProfile();
  ConfigureProfileProxy(proxied_profile);
  ConfigureProfileProxyManualGeo(proxied_profile, "GB", "Europe/London",
                                 51.5074, -0.1278);

  Browser* proxied_browser = CreateBrowser(proxied_profile);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      proxied_browser, OriginUrl("localhost", "/devtools-timezone")));
  content::WebContents* web_contents =
      proxied_browser->tab_strip_model()->GetActiveWebContents();
  constexpr char kTimezoneExpression[] =
      "Intl.DateTimeFormat().resolvedOptions().timeZone";
  ASSERT_EQ("Europe/London",
            content::EvalJs(web_contents, kTimezoneExpression).ExtractString());

  AttachToWebContents(web_contents);
  base::DictValue override_params;
  override_params.Set("timezoneId", "America/New_York");
  ASSERT_TRUE(SendCommandSync("Emulation.setTimezoneOverride",
                              std::move(override_params)));
  EXPECT_EQ("America/New_York",
            content::EvalJs(web_contents, kTimezoneExpression).ExtractString());

  base::DictValue clear_params;
  clear_params.Set("timezoneId", "");
  ASSERT_TRUE(SendCommandSync("Emulation.setTimezoneOverride",
                              std::move(clear_params)));
  EXPECT_EQ("Europe/London",
            content::EvalJs(web_contents, kTimezoneExpression).ExtractString());
  DetachProtocolClient();
}

IN_PROC_BROWSER_TEST_F(FingerprintBrowserProfileProxyBrowserTest,
                       ProfileProxyTimezonesRemainProfileScoped) {
  Profile* london_profile = CreateTestProfile();
  ConfigureProfileProxy(london_profile);
  ConfigureProfileProxyManualGeo(london_profile, "GB", "Europe/London", 51.5074,
                                 -0.1278);

  Profile* sydney_profile = CreateTestProfile();
  ConfigureProfileProxy(sydney_profile);
  ConfigureProfileProxyManualGeo(sydney_profile, "AU", "Australia/Sydney",
                                 -33.8688, 151.2093);

  Browser* london_browser = CreateBrowser(london_profile);
  Browser* sydney_browser = CreateBrowser(sydney_profile);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      london_browser, OriginUrl("london.test", "/london-timezone")));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      sydney_browser, OriginUrl("sydney.test", "/sydney-timezone")));

  content::WebContents* london_contents =
      london_browser->tab_strip_model()->GetActiveWebContents();
  content::WebContents* sydney_contents =
      sydney_browser->tab_strip_model()->GetActiveWebContents();
  constexpr char kTimezoneExpression[] =
      "Intl.DateTimeFormat().resolvedOptions().timeZone";
  EXPECT_EQ(
      "Europe/London",
      content::EvalJs(london_contents, kTimezoneExpression).ExtractString());
  EXPECT_EQ(
      "Australia/Sydney",
      content::EvalJs(sydney_contents, kTimezoneExpression).ExtractString());
  EXPECT_EQ(
      0, content::EvalJs(london_contents,
                         "new Date('2026-01-15T12:00:00Z').getTimezoneOffset()")
             .ExtractInt());
  EXPECT_EQ(-660, content::EvalJs(
                      sydney_contents,
                      "new Date('2026-01-15T12:00:00Z').getTimezoneOffset()")
                      .ExtractInt());
}

#if BUILDFLAG(ENABLE_TOR)
IN_PROC_BROWSER_TEST_F(FingerprintBrowserProfileProxyBrowserTest,
                       TorProfileKeepsDedicatedProxyService) {
  ConfigureProfileProxy(browser()->profile());

  Browser* tor_browser =
      TorProfileManager::SwitchToTorProfile(browser()->profile());
  ASSERT_TRUE(tor_browser->profile()->IsTor());

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      tor_browser, OriginUrl("tor-profile.test", "/tor-profile")));
  EXPECT_FALSE(ProxySawTargetContaining("/tor-profile"));
  EXPECT_FALSE(GetProxyService(tor_browser->profile()));
}
#endif
