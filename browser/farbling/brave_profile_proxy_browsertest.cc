/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/run_loop.h"
#include "base/synchronization/lock.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "brave/browser/fingerprint_browser/persona_service_factory.h"
#include "brave/browser/ui/webui/brave_settings_ui.h"
#include "brave/components/fingerprint_browser/browser/persona.h"
#include "brave/components/fingerprint_browser/browser/pref_names.h"
#include "brave/components/fingerprint_browser/browser/profile_proxy_config.h"
#include "brave/components/tor/buildflags/buildflags.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/delete_profile_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_metrics.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/profile_deletion_observer.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/permissions/permission_request_manager.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/webui_config_map.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
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

std::unique_ptr<net::test_server::BasicHttpResponse> TextResponse(
    std::string_view body) {
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/plain");
  response->set_content(body);
  return response;
}

bool SettingsRuntimeErrorIsVisible(content::WebContents* web_contents,
                                   std::string_view expected_error) {
  constexpr char kScript[] = R"js(
    (async () => {
      for (let i = 0; i < 200; ++i) {
        const root = window.testing?.fingerprintProfileProxySubpage;
        const errorRow = root?.getElementById('runtimeError');
        const text = errorRow?.innerText || '';
        if (errorRow &&
            errorRow.getAttribute('role') === 'alert' &&
            text.includes($1)) {
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
        const toggle = root?.getElementById('profileProxyEnabled');
        const risk = root?.getElementById('noProxyRisk');
        const text = risk?.innerText || '';
        if (!root || !toggle) {
          await new Promise(resolve => setTimeout(resolve, 25));
          continue;
        }

        if ($1) {
          if (!toggle.checked &&
              risk &&
              risk.getAttribute('role') === 'alert' &&
              text.includes('real network')) {
            return true;
          }
        } else if (toggle.checked) {
          if (!risk || getComputedStyle(risk).display === 'none') {
            return true;
          }
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

bool SettingsGeoWarningMatches(content::WebContents* web_contents,
                               bool expected_visible) {
  constexpr char kScript[] = R"js(
    (async () => {
      for (let i = 0; i < 200; ++i) {
        const root = window.testing?.fingerprintProfileProxySubpage;
        const warning = root?.getElementById('geoWarning');
        const text = warning?.innerText || '';
        if (!root) {
          await new Promise(resolve => setTimeout(resolve, 25));
          continue;
        }

        if ($1) {
          if (warning &&
              warning.getAttribute('role') === 'alert' &&
              text.includes('manual country')) {
            return true;
          }
        } else if (!warning) {
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

class FingerprintBrowserProfileProxyBrowserTest : public InProcessBrowserTest {
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

#if BUILDFLAG(ENABLE_TOR)
    net::ProxyConfigServiceTor::SetBypassTorProxyConfigForTesting(true);
    tor::TorNavigationThrottle::SetSkipWaitForTorConnectedForTesting(true);
#endif
  }

  void TearDownOnMainThread() override {
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

  void ConfigureProfileProxy(Profile* profile,
                             std::string_view username = "foo",
                             std::string_view password = "bar") {
    PrefService* prefs = profile->GetPrefs();
    prefs->SetBoolean(fingerprint_browser::prefs::kProfileProxyEnabled, true);
    prefs->SetString(fingerprint_browser::prefs::kProfileProxyScheme,
                     fingerprint_browser::prefs::kProfileProxySchemeHttp);
    prefs->SetString(fingerprint_browser::prefs::kProfileProxyHost,
                     "127.0.0.1");
    prefs->SetInteger(fingerprint_browser::prefs::kProfileProxyPort,
                      proxy_server_.port());
    prefs->SetString(fingerprint_browser::prefs::kProfileProxyUsername,
                     std::string(username));
    prefs->SetString(fingerprint_browser::prefs::kProfileProxyPassword,
                     std::string(password));
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
    if (auth == request.headers.end() ||
        auth->second != kExpectedProxyAuthorization) {
      {
        base::AutoLock lock(lock_);
        ++proxy_challenge_count_;
      }
      auto response = TextResponse("proxy auth required");
      response->set_code(net::HTTP_PROXY_AUTHENTICATION_REQUIRED);
      response->AddCustomHeader("Proxy-Authenticate",
                                "Basic realm=\"profile-proxy\"");
      return response;
    }

    base::AutoLock lock(lock_);
    saw_expected_proxy_authorization_ = true;
    return TextResponse(kProxyBody);
  }

  net::EmbeddedTestServer origin_server_;
  net::EmbeddedTestServer proxy_server_;

  mutable base::Lock lock_;
  std::map<std::string, int> origin_requests_by_path_;
  std::vector<std::string> proxy_request_targets_;
  int proxy_challenge_count_ GUARDED_BY(lock_) = 0;
  bool saw_expected_proxy_authorization_ GUARDED_BY(lock_) = false;
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
  ConfigureProfileProxy(proxied_profile, "wrong", "credentials");

  Browser* proxied_browser = CreateBrowser(proxied_profile);
  EXPECT_TRUE(ui_test_utils::NavigateToURL(
      proxied_browser, OriginUrl("profile-a.test", "/bad-auth")));
  base::RunLoop().RunUntilIdle();

  PrefService* prefs = proxied_profile->GetPrefs();
  EXPECT_EQ(net::ERR_INVALID_AUTH_CREDENTIALS,
            prefs->GetInteger(
                fingerprint_browser::prefs::kProfileProxyLastErrorCode));
  EXPECT_EQ(
      "Proxy authentication failed. Check username/password.",
      prefs->GetString(fingerprint_browser::prefs::kProfileProxyLastError));
  EXPECT_GE(ProxyChallengeCount(), 2);
  EXPECT_EQ(0, OriginRequestsForPath("/bad-auth"));

  ASSERT_TRUE(ui_test_utils::NavigateToURL(proxied_browser,
                                           GURL("brave://settings/privacy")));
  EXPECT_TRUE(SettingsRuntimeErrorIsVisible(
      proxied_browser->tab_strip_model()->GetActiveWebContents(),
      "Proxy authentication failed. Check username/password."));
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
                       UnknownProxyGeoShowsManualFallbackWarning) {
  Profile* proxied_profile = CreateTestProfile();
  ConfigureProfileProxy(proxied_profile);
  fingerprint_browser::SyncProfileProxyDerivedPrefs(
      *proxied_profile->GetPrefs());

  Browser* proxied_browser = CreateBrowser(proxied_profile);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(proxied_browser,
                                           GURL("brave://settings/privacy")));
  EXPECT_TRUE(SettingsGeoWarningMatches(
      proxied_browser->tab_strip_model()->GetActiveWebContents(),
      /*expected_visible=*/true));

  PrefService* prefs = proxied_profile->GetPrefs();
  prefs->SetBoolean(fingerprint_browser::prefs::kProfileProxyManualGeoEnabled,
                    true);
  prefs->SetString(
      fingerprint_browser::prefs::kProfileProxyManualGeoCountryCode, "GB");
  prefs->SetString(fingerprint_browser::prefs::kProfileProxyManualGeoTimezone,
                   "Europe/London");
  prefs->SetDouble(fingerprint_browser::prefs::kProfileProxyManualGeoLatitude,
                   51.5074);
  prefs->SetDouble(fingerprint_browser::prefs::kProfileProxyManualGeoLongitude,
                   -0.1278);
  fingerprint_browser::SyncProfileProxyDerivedPrefs(*prefs);

  ASSERT_TRUE(ui_test_utils::NavigateToURL(proxied_browser,
                                           GURL("brave://settings/privacy")));
  EXPECT_TRUE(SettingsGeoWarningMatches(
      proxied_browser->tab_strip_model()->GetActiveWebContents(),
      /*expected_visible=*/false));
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
                       ProfileProxyGeolocationUsesProfileGeo) {
  Profile* proxied_profile = CreateTestProfile();
  ConfigureProfileProxy(proxied_profile);
  ConfigureProfileProxyManualGeo(proxied_profile, "GB", "Europe/London",
                                 51.5074, -0.1278);

  Browser* proxied_browser = CreateBrowser(proxied_profile);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      proxied_browser, OriginUrl("localhost", "/profile-geo")));
  content::WebContents* web_contents =
      proxied_browser->tab_strip_model()->GetActiveWebContents();
  permissions::PermissionRequestManager::FromWebContents(web_contents)
      ->set_auto_response_for_test(
          permissions::PermissionRequestManager::AutoResponseType::ACCEPT_ALL);

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
  EXPECT_EQ("Europe/London",
            web_contents->GetMutableRendererPrefs()
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
  EXPECT_EQ("Australia/Sydney",
            web_contents->GetMutableRendererPrefs()
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
}
#endif
