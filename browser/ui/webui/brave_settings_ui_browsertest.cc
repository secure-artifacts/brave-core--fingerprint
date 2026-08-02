// Copyright (c) 2025 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/browser/ui/webui/brave_settings_ui.h"

#include "chrome/browser/profiles/profile_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"

class BraveSettingsUIBrowserTest : public InProcessBrowserTest {
 public:
  BraveSettingsUIBrowserTest() = default;
  ~BraveSettingsUIBrowserTest() override = default;
};

IN_PROC_BROWSER_TEST_F(BraveSettingsUIBrowserTest, LoadsBraveSettingsUI) {
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://settings/")));

  auto* web_contents = browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_NE(web_contents, nullptr);
  EXPECT_TRUE(content::WaitForLoadStop(web_contents));

  auto* web_ui = web_contents->GetPrimaryMainFrame()->GetWebUI();
  ASSERT_NE(web_ui, nullptr);
  EXPECT_TRUE(web_ui->GetController()->GetAs<BraveSettingsUI>());

  const auto background = content::EvalJs(web_contents, R"(
        (() => {
          const findSettingsMain = (root) => {
            for (const element of root.querySelectorAll('*')) {
              if (element.tagName.toLowerCase() === 'settings-main') {
                return element;
              }
              if (element.shadowRoot) {
                const result = findSettingsMain(element.shadowRoot);
                if (result) {
                  return result;
                }
              }
            }
            return null;
          };
          const settingsMain = findSettingsMain(document);
          return settingsMain ? getComputedStyle(settingsMain).backgroundColor
                              : 'missing';
        })()
      )")
                              .ExtractString();
  EXPECT_NE(background, "missing");
  EXPECT_NE(background, "rgb(255, 0, 0)");
}

IN_PROC_BROWSER_TEST_F(BraveSettingsUIBrowserTest,
                       LoadsFingerprintProfileProxySettings) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                           GURL("chrome://settings/privacy")));

  auto* web_contents = browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_NE(web_contents, nullptr);
  EXPECT_TRUE(content::WaitForLoadStop(web_contents));

  const auto visibility = content::EvalJs(web_contents, R"(
    (async () => {
      const findElement = (root, selector) => {
        const direct = root.querySelector(selector);
        if (direct) {
          return direct;
        }
        for (const element of root.querySelectorAll('*')) {
          if (element.shadowRoot) {
            const result = findElement(element.shadowRoot, selector);
            if (result) {
              return result;
            }
          }
        }
        return null;
      };
      for (let attempt = 0; attempt < 100; ++attempt) {
        const link = findElement(
            document, '#fingerprintProfileProxyLinkRow');
        if (link) {
          link.click();
          break;
        }
        await new Promise(resolve => setTimeout(resolve, 50));
      }
      for (let attempt = 0; attempt < 100; ++attempt) {
        if (location.pathname === '/fingerprintProfileProxy') {
          break;
        }
        await new Promise(resolve => setTimeout(resolve, 50));
      }
      const findProxyPage = (root) => {
        const direct = root.querySelector(
            'settings-fingerprint-profile-proxy-subpage');
        if (direct) {
          return direct;
        }
        for (const element of root.querySelectorAll('*')) {
          if (element.shadowRoot) {
            const result = findProxyPage(element.shadowRoot);
            if (result) {
              return result;
            }
          }
        }
        return null;
      };
      for (let attempt = 0; attempt < 100; ++attempt) {
        const proxyPage = findProxyPage(document);
        if (proxyPage && proxyPage.getClientRects().length > 0 &&
            proxyPage.shadowRoot?.querySelector('#host')) {
          return 'visible';
        }
        await new Promise(resolve => setTimeout(resolve, 50));
      }
      return 'missing';
    })()
  )")
                              .ExtractString();
  EXPECT_EQ(visibility, "visible");
}

// Test that chrome://settings loads without crashing in guest profiles.
// This verifies that all Mojo interface bindings properly handle null services
// for guest profiles (e.g., BraveOriginService).
IN_PROC_BROWSER_TEST_F(BraveSettingsUIBrowserTest, GuestProfileLoadsSettings) {
  // Switch to guest profile and wait for browser to open
  profiles::SwitchToGuestProfile();
  Browser* guest_browser = ui_test_utils::WaitForBrowserToOpen();
  ASSERT_NE(guest_browser, nullptr);

  // Navigate to settings - this should trigger all BindInterface calls
  // including the one for BraveOriginSettingsHandler which needs to handle
  // a null service for guest profiles
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(guest_browser, GURL("chrome://settings/")));

  // Verify navigation succeeded and page loaded
  auto* web_contents = guest_browser->tab_strip_model()->GetActiveWebContents();
  ASSERT_NE(web_contents, nullptr);
  EXPECT_TRUE(content::WaitForLoadStop(web_contents));
  EXPECT_EQ(web_contents->GetURL(), GURL("chrome://settings/"));
}
