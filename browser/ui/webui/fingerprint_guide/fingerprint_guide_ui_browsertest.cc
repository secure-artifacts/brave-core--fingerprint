/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "base/command_line.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "ui/base/ui_base_switches.h"

namespace {

class FingerprintGuideUIBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitchASCII(switches::kLang, "en-US");
  }
};

IN_PROC_BROWSER_TEST_F(FingerprintGuideUIBrowserTest, LoadsChineseGuide) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                           GURL("brave://fingerprint-guide/")));
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::WaitForLoadStop(contents));

  EXPECT_EQ(content::EvalJs(contents, "document.documentElement.lang"),
            "zh-CN");
  EXPECT_EQ(content::EvalJs(contents,
                            "document.querySelector('h1').textContent.trim()"),
            "指纹浏览器使用指南");
  EXPECT_TRUE(content::EvalJs(contents, R"(
    [...document.querySelectorAll('a[data-guide-target]')]
        .map(link => link.getAttribute('href'))
        .includes('brave://settings/fingerprintProfileProxy')
  )")
                  .ExtractBool());
}

IN_PROC_BROWSER_TEST_F(FingerprintGuideUIBrowserTest, OpensAllGuideTargets) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                           GURL("brave://fingerprint-guide/")));
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::WaitForLoadStop(contents));

  const content::EvalJsResult links = content::EvalJs(contents, R"(
    [...document.querySelectorAll('a[data-guide-target]')]
        .map(link => link.getAttribute('href'))
  )");
  const base::ListValue& values = links.ExtractList();
  EXPECT_EQ(values.size(), 5u);
  EXPECT_EQ(values[0].GetString(), "brave://profile-picker/");
  EXPECT_EQ(values[1].GetString(), "brave://settings/fingerprintProfileProxy");
  EXPECT_EQ(values[2].GetString(), "brave://fingerprint-test/");
  EXPECT_EQ(values[3].GetString(), "brave://diagnostics/");
  EXPECT_EQ(values[4].GetString(), "brave://crashes/");
}

IN_PROC_BROWSER_TEST_F(FingerprintGuideUIBrowserTest,
                       CustomPagesStayChineseWithEnglishLocale) {
  for (const auto& [url, title] : {
           std::pair{"brave://fingerprint-test/", "指纹检测"},
           std::pair{"brave://diagnostics/", "导出诊断信息"},
       }) {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL(url)));
    content::WebContents* contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    ASSERT_TRUE(content::WaitForLoadStop(contents));
    EXPECT_EQ(content::EvalJs(contents, "document.title").ExtractString(),
              title);
    EXPECT_EQ(content::EvalJs(contents, "document.documentElement.lang"),
              "zh-CN");
  }
}

}  // namespace
