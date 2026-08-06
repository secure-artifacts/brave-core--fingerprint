/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/download/download_crx_util.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/extensions/extension_install_prompt.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/crx_file/crx_verifier.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "extensions/browser/extension_dialog_auto_confirm.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/extension_util.h"
#include "extensions/browser/install/crx_install_error.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"

namespace extensions {

namespace {

class PromptResult {
 public:
  void Succeeded(const Extension& extension, bool prompt_shown) {
    extension_id_ = extension.id();
    prompt_shown_ = prompt_shown;
    run_loop_.Quit();
  }

  void Failed(bool prompt_shown) {
    prompt_shown_ = prompt_shown;
    failed_ = true;
    run_loop_.Quit();
  }

  bool Wait() {
    base::OneShotTimer timeout;
    timeout.Start(
        FROM_HERE, base::Seconds(15),
        base::BindOnce(&PromptResult::OnTimeout, base::Unretained(this)));
    run_loop_.Run();
    return !timed_out_;
  }
  bool failed() const { return failed_; }
  bool prompt_shown() const { return prompt_shown_; }
  const ExtensionId& extension_id() const { return extension_id_; }

 private:
  void OnTimeout() {
    timed_out_ = true;
    run_loop_.Quit();
  }

  base::RunLoop run_loop_;
  ExtensionId extension_id_;
  bool failed_ = false;
  bool prompt_shown_ = false;
  bool timed_out_ = false;
};

class UpdateResult {
 public:
  void Complete(const std::optional<CrxInstallError>& error) {
    error_ = error;
    run_loop_.Quit();
  }

  bool Wait() {
    base::OneShotTimer timeout;
    timeout.Start(
        FROM_HERE, base::Seconds(15),
        base::BindOnce(&UpdateResult::OnTimeout, base::Unretained(this)));
    run_loop_.Run();
    return !timed_out_;
  }

  const std::optional<CrxInstallError>& error() const { return error_; }

 private:
  void OnTimeout() {
    timed_out_ = true;
    run_loop_.Quit();
  }

  base::RunLoop run_loop_;
  std::optional<CrxInstallError> error_;
  bool timed_out_ = false;
};

class TestInstallPrompt : public ExtensionInstallPrompt {
 public:
  TestInstallPrompt(content::WebContents* web_contents,
                    std::shared_ptr<PromptResult> result)
      : ExtensionInstallPrompt(web_contents), result_(std::move(result)) {}

  void OnInstallSuccess(scoped_refptr<const Extension> extension,
                        SkBitmap*) override {
    result_->Succeeded(*extension, did_call_show_dialog());
  }

  void OnInstallFailure(const CrxInstallError&) override {
    result_->Failed(did_call_show_dialog());
  }

 private:
  std::shared_ptr<PromptResult> result_;
};

class InternalExtensionStoreBrowserTest : public ExtensionBrowserTest {
 public:
  enum class ServedPackage {
    kValid,
    kCorrupt,
    kInvalidUpdateUrl,
  };

  InternalExtensionStoreBrowserTest()
      : https_server_(net::EmbeddedTestServer::TYPE_HTTPS) {}

  void SetUpOnMainThread() override {
    ExtensionBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    host_resolver()->AddRule("*", "127.0.0.1");
    https_server_.SetSSLConfig(net::EmbeddedTestServer::CERT_TEST_NAMES);
    https_server_.RegisterRequestHandler(
        base::BindRepeating(&InternalExtensionStoreBrowserTest::HandleRequest,
                            base::Unretained(this)));
    ASSERT_TRUE(https_server_.Start());
    origin_override_ =
        util::OverrideBraveInternalExtensionStoreOriginForTesting(
            https_server_.GetURL("a.test", "/").DeprecatedGetOriginAsURL());
    BuildSignedCrx();
  }

  void TearDownOnMainThread() override {
    download_crx_util::SetMockInstallPromptForTesting(nullptr);
    ExtensionBrowserTest::TearDownOnMainThread();
    origin_override_.reset();
  }

 protected:
  void RunDownload(ScopedTestDialogAutoConfirm::AutoConfirm action,
                   ServedPackage served_package) {
    served_package_ = served_package;
    auto result = std::make_shared<PromptResult>();
    ScopedTestDialogAutoConfirm auto_confirm(action);
    download_crx_util::SetMockInstallPromptForTesting(
        std::make_unique<TestInstallPrompt>(GetActiveWebContents(), result));

    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), https_server_.GetURL("a.test", "/crx/catalog.html")));
    content::ExecuteScriptAsync(GetActiveWebContents(),
                                "document.querySelector('a').click()");
    const bool completed = result->Wait();
    if (!completed) {
      download_crx_util::SetMockInstallPromptForTesting(nullptr);
    }
    ASSERT_TRUE(completed);

    if (action == ScopedTestDialogAutoConfirm::ACCEPT &&
        served_package == ServedPackage::kValid) {
      EXPECT_FALSE(result->failed());
      EXPECT_TRUE(result->prompt_shown());
      EXPECT_EQ(extension_id_, result->extension_id());
      EXPECT_TRUE(
          extension_registry()->enabled_extensions().Contains(extension_id_));
    } else {
      EXPECT_TRUE(result->failed());
      EXPECT_FALSE(extension_registry()->GetInstalledExtension(extension_id_));
      if (served_package == ServedPackage::kValid) {
        EXPECT_TRUE(result->prompt_shown());
      } else {
        EXPECT_FALSE(result->prompt_shown());
      }
    }
    EXPECT_TRUE(
        GetActiveWebContents()->GetPrimaryMainFrame()->IsRenderFrameLive());
  }

  void RunAutomaticUpdate(const base::FilePath& unpacked_dir,
                          bool expect_success,
                          std::string_view expected_version) {
    auto result = std::make_shared<UpdateResult>();
    ExtensionSystem::Get(profile())->InstallUpdate(
        extension_id_, public_key_, unpacked_dir,
        /*install_immediately=*/true,
        base::BindOnce(
            [](std::shared_ptr<UpdateResult> result,
               const std::optional<CrxInstallError>& error) {
              result->Complete(error);
            },
            result));
    ASSERT_TRUE(result->Wait());

    EXPECT_EQ(expect_success, !result->error().has_value());
    if (!expect_success) {
      ASSERT_TRUE(result->error());
      EXPECT_EQ(CrxInstallErrorDetail::MANIFEST_INVALID,
                result->error()->detail());
    }
    const Extension* installed_extension =
        extension_registry()->GetInstalledExtension(extension_id_);
    ASSERT_TRUE(installed_extension);
    EXPECT_EQ(expected_version, installed_extension->VersionString());
  }

 private:
  base::FilePath CreateUnpackedUpdate(std::string_view directory_name,
                                      std::string_view version,
                                      const GURL& update_url) {
    const base::FilePath update_dir =
        temp_dir_.GetPath().AppendASCII(directory_name);
    EXPECT_TRUE(base::CreateDirectory(update_dir));
    const std::string version_string(version);
    const std::string update_url_string = update_url.spec();
    EXPECT_TRUE(base::WriteFile(
        update_dir.AppendASCII("manifest.json"),
        base::StringPrintf(
            R"({"manifest_version":3,"name":"Internal store test","version":"%s","update_url":"%s"})",
            version_string.c_str(), update_url_string.c_str())));
    return update_dir;
  }

  void BuildSignedCrx() {
    const base::FilePath root = temp_dir_.GetPath().AppendASCII("extension");
    ASSERT_TRUE(base::CreateDirectory(root));
    const base::FilePath manifest = root.AppendASCII("manifest.json");
    ASSERT_TRUE(base::WriteFile(
        manifest,
        R"({"manifest_version":3,"name":"Internal store test","version":"1.0","update_url":"https://invalid.test/crx/pending/update.xml"})"));

    const base::FilePath first_crx =
        temp_dir_.GetPath().AppendASCII("first.crx");
    const base::FilePath key = temp_dir_.GetPath().AppendASCII("extension.pem");
    ASSERT_FALSE(
        PackExtensionWithOptions(root, first_crx, base::FilePath(), key)
            .empty());
    ASSERT_EQ(crx_file::VerifierResult::OK_FULL,
              crx_file::Verify(first_crx, crx_file::VerifierFormat::CRX3, {},
                               {}, &public_key_, &extension_id_, nullptr));
    ASSERT_TRUE(base::ReadFileToString(first_crx, &invalid_update_crx_bytes_));
    ASSERT_FALSE(invalid_update_crx_bytes_.empty());

    const GURL update_url = https_server_.GetURL(
        "a.test",
        base::StringPrintf("/crx/%s/update.xml", extension_id_.c_str()));
    ASSERT_TRUE(base::WriteFile(
        manifest,
        base::StringPrintf(
            R"({"manifest_version":3,"name":"Internal store test","version":"1.0","update_url":"%s"})",
            update_url.spec().c_str())));
    const base::FilePath final_crx =
        temp_dir_.GetPath().AppendASCII("final.crx");
    ASSERT_FALSE(
        PackExtensionWithOptions(root, final_crx, key, base::FilePath())
            .empty());
    ASSERT_TRUE(base::ReadFileToString(final_crx, &crx_bytes_));
    ASSERT_FALSE(crx_bytes_.empty());

    valid_update_dir_ = CreateUnpackedUpdate("valid-update", "2.0", update_url);
    invalid_update_dir_ = CreateUnpackedUpdate(
        "invalid-update", "3.0",
        GURL("https://invalid.test/crx/extension/update.xml"));
  }

  std::unique_ptr<net::test_server::HttpResponse> HandleRequest(
      const net::test_server::HttpRequest& request) {
    auto response = std::make_unique<net::test_server::BasicHttpResponse>();
    if (request.relative_url == "/crx/catalog.html") {
      response->set_content_type("text/html");
      response->set_content("<a href='/crx/download.crx'>Install</a>");
      return response;
    }
    if (request.relative_url == "/crx/download.crx") {
      response->set_content_type("application/octet-stream");
      response->AddCustomHeader("Content-Disposition",
                                "attachment; filename=download.crx");
      response->AddCustomHeader("X-Content-Type-Options", "nosniff");
      switch (served_package_) {
        case ServedPackage::kValid:
          response->set_content(crx_bytes_);
          break;
        case ServedPackage::kCorrupt:
          response->set_content("not a crx");
          break;
        case ServedPackage::kInvalidUpdateUrl:
          response->set_content(invalid_update_crx_bytes_);
          break;
      }
      return response;
    }
    if (request.relative_url ==
        base::StringPrintf("/crx/%s/update.xml", extension_id_.c_str())) {
      response->set_content_type("text/xml");
      response->set_content(
          "<?xml version='1.0'?><gupdate "
          "xmlns='http://www.google.com/update2/response' protocol='2.0'/>");
      return response;
    }
    return nullptr;
  }

  net::EmbeddedTestServer https_server_;
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<base::AutoReset<GURL>> origin_override_;
  ExtensionId extension_id_;
  std::string public_key_;
  std::string crx_bytes_;
  std::string invalid_update_crx_bytes_;
  base::FilePath valid_update_dir_;
  base::FilePath invalid_update_dir_;
  ServedPackage served_package_ = ServedPackage::kValid;
};

IN_PROC_BROWSER_TEST_F(InternalExtensionStoreBrowserTest, AcceptInstalls) {
  RunDownload(ScopedTestDialogAutoConfirm::ACCEPT, ServedPackage::kValid);
}

IN_PROC_BROWSER_TEST_F(InternalExtensionStoreBrowserTest,
                       CancelDoesNotInstall) {
  RunDownload(ScopedTestDialogAutoConfirm::CANCEL, ServedPackage::kValid);
}

IN_PROC_BROWSER_TEST_F(InternalExtensionStoreBrowserTest,
                       CorruptCrxRejectedAndBrowserAlive) {
  RunDownload(ScopedTestDialogAutoConfirm::ACCEPT, ServedPackage::kCorrupt);
}

IN_PROC_BROWSER_TEST_F(InternalExtensionStoreBrowserTest,
                       InvalidUpdateUrlRejectedBeforePrompt) {
  RunDownload(ScopedTestDialogAutoConfirm::ACCEPT,
              ServedPackage::kInvalidUpdateUrl);
}

IN_PROC_BROWSER_TEST_F(InternalExtensionStoreBrowserTest,
                       PolicyCannotBypassInternalUpdateUrlRestriction) {
  auto policy_override =
      download_crx_util::OverrideOffstoreInstallAllowedForTesting(true);
  ASSERT_TRUE(policy_override);
  RunDownload(ScopedTestDialogAutoConfirm::ACCEPT,
              ServedPackage::kInvalidUpdateUrl);
}

IN_PROC_BROWSER_TEST_F(InternalExtensionStoreBrowserTest,
                       AutomaticUpdatesRemainOnInternalStore) {
  RunDownload(ScopedTestDialogAutoConfirm::ACCEPT, ServedPackage::kValid);
  RunAutomaticUpdate(valid_update_dir_, true, "2.0");
  RunAutomaticUpdate(invalid_update_dir_, false, "2.0");
}

}  // namespace

}  // namespace extensions
