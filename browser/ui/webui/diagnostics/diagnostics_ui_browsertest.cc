/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include <optional>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/crash/core/app/crashpad.h"
#include "components/metrics/metrics_pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "third_party/zlib/google/zip_reader.h"
#include "ui/shell_dialogs/fake_select_file_dialog.h"

namespace {

class DiagnosticsUIBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    dialog_factory_ = ui::FakeSelectFileDialog::RegisterFactory();
    dialog_factory_->SetOpenCallback(base::DoNothing());
  }

  void TearDownOnMainThread() override {
    ui::SelectFileDialog::SetFactory(nullptr);
    InProcessBrowserTest::TearDownOnMainThread();
  }

 protected:
  content::WebContents* OpenDiagnostics() {
    EXPECT_TRUE(
        ui_test_utils::NavigateToURL(browser(), GURL("brave://diagnostics/")));
    content::WebContents* contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    EXPECT_TRUE(content::WaitForLoadStop(contents));
    return contents;
  }

  void StartExport(content::WebContents* contents) {
    EXPECT_TRUE(content::EvalJs(contents, R"(
      (async () => {
        for (let attempt = 0; attempt < 100; ++attempt) {
          const confirmation = document.querySelector('#privacy-confirm');
          if (confirmation && !confirmation.disabled) {
            confirmation.checked = true;
            confirmation.dispatchEvent(new Event('change'));
            document.querySelector('#export').click();
            return true;
          }
          await new Promise(resolve => setTimeout(resolve, 50));
        }
        return false;
      })()
    )")
                    .ExtractBool());
    ASSERT_TRUE(base::test::RunUntil(
        [&]() { return dialog_factory_->GetLastDialog() != nullptr; }));
  }

  std::string WaitForExportState(content::WebContents* contents) {
    return content::EvalJs(contents, R"(
      (async () => {
        for (let attempt = 0; attempt < 200; ++attempt) {
          const state = document.querySelector('#export-status')?.dataset.state;
          if (state && state !== 'progress') {
            return state;
          }
          await new Promise(resolve => setTimeout(resolve, 50));
        }
        return 'timeout';
      })()
    )")
        .ExtractString();
  }

  raw_ptr<ui::FakeSelectFileDialog::Factory> dialog_factory_ = nullptr;
};

IN_PROC_BROWSER_TEST_F(DiagnosticsUIBrowserTest,
                       LoadsAndRequiresPrivacyConfirmation) {
  content::WebContents* contents = OpenDiagnostics();
  const content::EvalJsResult result = content::EvalJs(contents, R"(
    (async () => {
      for (let attempt = 0; attempt < 100; ++attempt) {
        const confirmation = document.querySelector('#privacy-confirm');
        if (confirmation && !confirmation.disabled) {
          return {
            availability: document.querySelector('#availability').textContent.trim(),
            confirmationChecked: confirmation.checked,
            exportDisabled: document.querySelector('#export').disabled,
            guide: document.querySelector('a[href="brave://fingerprint-guide/"]')?.textContent.trim(),
            title: document.querySelector('h1').textContent.trim(),
          };
        }
        await new Promise(resolve => setTimeout(resolve, 50));
      }
      return {title: 'timeout'};
    })()
  )");
  const base::DictValue& state = result.ExtractDict();
  EXPECT_EQ(*state.FindString("title"), "导出诊断信息");
  EXPECT_EQ(*state.FindString("guide"), "使用指南");
  const std::string* availability = state.FindString("availability");
  ASSERT_TRUE(availability);
  EXPECT_FALSE(availability->empty());
  EXPECT_FALSE(state.FindBool("confirmationChecked").value_or(true));
  EXPECT_TRUE(state.FindBool("exportDisabled").value_or(false));
}

IN_PROC_BROWSER_TEST_F(DiagnosticsUIBrowserTest, ReportsSaveCancellation) {
  content::WebContents* contents = OpenDiagnostics();
  StartExport(contents);
  dialog_factory_->GetLastDialog()->CallFileSelectionCanceled();
  EXPECT_EQ(WaitForExportState(contents), "cancelled");
}

IN_PROC_BROWSER_TEST_F(DiagnosticsUIBrowserTest, ReportsExportFailure) {
  content::WebContents* contents = OpenDiagnostics();
  StartExport(contents);
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath target =
      temp_dir.GetPath().AppendASCII("missing").AppendASCII("diagnostics.zip");
  ASSERT_TRUE(
      dialog_factory_->GetLastDialog()->CallFileSelected(target, "zip"));
  EXPECT_EQ(WaitForExportState(contents), "error");
  EXPECT_FALSE(base::PathExists(target));
}

IN_PROC_BROWSER_TEST_F(DiagnosticsUIBrowserTest,
                       ExportsLocallyWhenTelemetryIsDisabled) {
  g_browser_process->local_state()->SetBoolean(
      metrics::prefs::kMetricsReportingEnabled, false);
  content::WebContents* contents = OpenDiagnostics();
  StartExport(contents);
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath target =
      temp_dir.GetPath().AppendASCII("diagnostics.zip");
  ASSERT_TRUE(
      dialog_factory_->GetLastDialog()->CallFileSelected(target, "zip"));
  EXPECT_EQ(WaitForExportState(contents), "success");
  EXPECT_TRUE(base::PathExists(target));

  zip::ZipReader reader;
  ASSERT_TRUE(reader.Open(target));
  bool found_manifest = false;
  bool found_checksums = false;
  std::optional<base::DictValue> browser_state;
  while (const auto* entry = reader.Next()) {
    found_manifest |=
        entry->path == base::FilePath(FILE_PATH_LITERAL("manifest.json"));
    found_checksums |=
        entry->path == base::FilePath(FILE_PATH_LITERAL("checksums.sha256"));
    if (entry->path ==
        base::FilePath(FILE_PATH_LITERAL("state/browser.json"))) {
      std::string browser_json;
      ASSERT_TRUE(reader.ExtractCurrentEntryToString(&browser_json));
      browser_state =
          base::JSONReader::ReadDict(browser_json, base::JSON_PARSE_RFC);
    }
  }
  EXPECT_TRUE(reader.ok());
  EXPECT_TRUE(found_manifest);
  EXPECT_TRUE(found_checksums);
  ASSERT_TRUE(browser_state);
  const std::string* native_report_source =
      browser_state->FindString("nativeReportSource");
  ASSERT_TRUE(native_report_source);
#if BUILDFLAG(IS_WIN)
  EXPECT_EQ(*native_report_source, "windows_crashpad");
#elif BUILDFLAG(IS_MAC)
  EXPECT_EQ(*native_report_source, "macos_diagnostic_reports");
#else
  EXPECT_EQ(*native_report_source, "unavailable_on_platform");
#endif
}

IN_PROC_BROWSER_TEST_F(DiagnosticsUIBrowserTest, CrashesPageShowsLocalActions) {
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("brave://crashes/")));
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::WaitForLoadStop(contents));
  EXPECT_TRUE(content::EvalJs(contents, R"(
    !document.querySelector('#localDiagnosticsActions').hidden &&
        !document.querySelector('#exportDiagnostics').hidden &&
        !document.querySelector('#openCrashFolder').hidden &&
        Boolean(document.querySelector('#fingerprintGuide'))
  )")
                  .ExtractBool());
  const auto crashpad_path = crash_reporter::GetCrashpadDatabasePath();
  if (!crashpad_path) {
    return;
  }
  EXPECT_EQ(content::EvalJs(contents,
                            "document.querySelector('#crashFolderPath')"
                            ".textContent.trim()")
                .ExtractString(),
            base::UTF16ToUTF8(crashpad_path->LossyDisplayName()));
}

IN_PROC_BROWSER_TEST_F(DiagnosticsUIBrowserTest,
                       GuestCrashesPageKeepsChineseGuide) {
  profiles::SwitchToGuestProfile();
  Browser* guest_browser = ui_test_utils::WaitForBrowserToOpen();
  ASSERT_TRUE(guest_browser);
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(guest_browser, GURL("brave://crashes/")));
  content::WebContents* contents =
      guest_browser->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::WaitForLoadStop(contents));
  EXPECT_TRUE(content::EvalJs(contents, R"(
    document.querySelector('#exportDiagnostics').hidden &&
        document.querySelector('#openCrashFolder').hidden &&
        document.querySelector('#crashFolderPath').hidden &&
        !document.querySelector('#fingerprintGuide').hidden &&
        document.querySelector('#fingerprintGuide').textContent.trim() ===
            '指纹浏览器使用指南'
  )")
                  .ExtractBool());
}

}  // namespace
