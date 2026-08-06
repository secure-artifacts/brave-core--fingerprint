/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include <memory>
#include <string>
#include <vector>

#include "chrome/browser/download/download_crx_util.h"
#include "chrome/browser/extensions/extension_install_prompt.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/base/testing_profile_manager.h"
#include "components/download/public/common/download_item.h"
#include "components/download/public/common/mock_download_item.h"
#include "content/public/test/browser_task_environment.h"
#include "extensions/browser/crx_installer.h"
#include "extensions/browser/extension_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace extensions::util {

namespace {

using ::testing::AnyNumber;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::StrictMock;

using StrictMockDownloadItem = StrictMock<download::MockDownloadItem>;

class InternalExtensionStoreIntegrationTest : public testing::Test {
 protected:
  content::BrowserTaskEnvironment task_environment_;
};

struct DownloadItemData {
  bool has_user_gesture = true;
  download::DownloadItem::TargetDisposition target_disposition =
      download::DownloadItem::TARGET_DISPOSITION_OVERWRITE;
  GURL final_url;
  GURL original_url;
  GURL referrer_url;
  std::vector<GURL> url_chain;
  std::string mime_type = "application/x-chrome-extension";
  std::string original_mime_type;
  std::string suggested_filename = "extension.crx";
  std::string content_disposition = "attachment; filename=\"extension.crx\"";
};

DownloadItemData MakeValidDownloadItemData() {
  DownloadItemData data;
  data.final_url =
      GURL("https://plugin.afferdmail.com/crx/final-extension.crx");
  data.original_url =
      GURL("https://plugin.afferdmail.com/crx/source-extension.crx");
  data.referrer_url =
      GURL("https://plugin.afferdmail.com/crx/extension-details");
  data.url_chain = {data.original_url, data.final_url};
  return data;
}

void ExpectDownloadItem(StrictMockDownloadItem& download_item,
                        const DownloadItemData& data) {
  EXPECT_CALL(download_item, HasUserGesture())
      .Times(AnyNumber())
      .WillRepeatedly(Return(data.has_user_gesture));
  EXPECT_CALL(download_item, GetTargetDisposition())
      .Times(AnyNumber())
      .WillRepeatedly(Return(data.target_disposition));
  EXPECT_CALL(download_item, GetURL())
      .Times(AnyNumber())
      .WillRepeatedly(ReturnRef(data.final_url));
  EXPECT_CALL(download_item, GetOriginalUrl())
      .Times(AnyNumber())
      .WillRepeatedly(ReturnRef(data.original_url));
  EXPECT_CALL(download_item, GetReferrerUrl())
      .Times(AnyNumber())
      .WillRepeatedly(ReturnRef(data.referrer_url));
  EXPECT_CALL(download_item, GetUrlChain())
      .Times(AnyNumber())
      .WillRepeatedly(ReturnRef(data.url_chain));
  EXPECT_CALL(download_item, GetMimeType())
      .Times(AnyNumber())
      .WillRepeatedly(Return(data.mime_type));
  EXPECT_CALL(download_item, GetOriginalMimeType())
      .Times(AnyNumber())
      .WillRepeatedly(Return(data.original_mime_type));
  EXPECT_CALL(download_item, GetSuggestedFilename())
      .Times(AnyNumber())
      .WillRepeatedly(Return(data.suggested_filename));
  EXPECT_CALL(download_item, GetContentDisposition())
      .Times(AnyNumber())
      .WillRepeatedly(Return(data.content_disposition));
}

TEST(InternalExtensionStoreDownloadTest, AllowsExactStoreOriginAndCrxPaths) {
  const DownloadItemData data = MakeValidDownloadItemData();
  StrictMockDownloadItem download_item;
  ExpectDownloadItem(download_item, data);

  EXPECT_TRUE(IsBraveInternalExtensionStoreDownload(download_item));
}

TEST(InternalExtensionStoreDownloadTest, RequiresUserGesture) {
  DownloadItemData data = MakeValidDownloadItemData();
  data.has_user_gesture = false;
  StrictMockDownloadItem download_item;
  ExpectDownloadItem(download_item, data);

  EXPECT_FALSE(IsBraveInternalExtensionStoreDownload(download_item));
}

TEST(InternalExtensionStoreDownloadTest,
     AcceptsSupportedCurrentOrOriginalMimeType) {
  struct TestCase {
    const char* name;
    const char* mime_type;
    const char* original_mime_type;
  };
  constexpr TestCase kTestCases[] = {
      {"ChromeExtension", "application/x-chrome-extension", "text/plain"},
      {"OctetStream", "application/octet-stream", "text/plain"},
      {"OriginalChromeExtension", "text/plain",
       "application/x-chrome-extension"},
      {"OriginalOctetStream", "text/plain", "application/octet-stream"},
  };

  for (const auto& test_case : kTestCases) {
    SCOPED_TRACE(test_case.name);
    DownloadItemData data = MakeValidDownloadItemData();
    data.mime_type = test_case.mime_type;
    data.original_mime_type = test_case.original_mime_type;
    StrictMockDownloadItem download_item;
    ExpectDownloadItem(download_item, data);

    EXPECT_TRUE(IsBraveInternalExtensionStoreDownload(download_item));
  }
}

TEST(InternalExtensionStoreDownloadTest, RejectsUnsupportedMimeType) {
  DownloadItemData data = MakeValidDownloadItemData();
  data.mime_type = "text/plain";
  data.original_mime_type = "application/zip";
  StrictMockDownloadItem download_item;
  ExpectDownloadItem(download_item, data);

  EXPECT_FALSE(IsBraveInternalExtensionStoreDownload(download_item));
}

TEST(InternalExtensionStoreDownloadTest, RejectsPromptTargetDisposition) {
  DownloadItemData data = MakeValidDownloadItemData();
  data.target_disposition = download::DownloadItem::TARGET_DISPOSITION_PROMPT;
  StrictMockDownloadItem download_item;
  ExpectDownloadItem(download_item, data);

  EXPECT_FALSE(IsBraveInternalExtensionStoreDownload(download_item));
}

TEST(InternalExtensionStoreDownloadTest, ValidatesSuggestedAndHeaderFilenames) {
  struct TestCase {
    const char* name;
    const char* suggested_filename;
    const char* content_disposition;
    bool expected;
  };
  constexpr TestCase kTestCases[] = {
      {"CrxFilenames", "extension.crx",
       "attachment; filename=\"extension.crx\"", true},
      {"EmptyHeader", "extension.crx", "", true},
      {"WrongSuggestedSuffix", "extension.zip",
       "attachment; filename=\"extension.crx\"", false},
      {"WrongHeaderSuffix", "extension.crx",
       "attachment; filename=\"extension.zip\"", false},
  };

  for (const auto& test_case : kTestCases) {
    SCOPED_TRACE(test_case.name);
    DownloadItemData data = MakeValidDownloadItemData();
    data.suggested_filename = test_case.suggested_filename;
    data.content_disposition = test_case.content_disposition;
    StrictMockDownloadItem download_item;
    ExpectDownloadItem(download_item, data);

    EXPECT_EQ(test_case.expected,
              IsBraveInternalExtensionStoreDownload(download_item));
  }
}

TEST(InternalExtensionStoreDownloadTest, RejectsInvalidStoreOriginsAndPaths) {
  struct TestCase {
    const char* name;
    const char* final_url;
    const char* original_url;
    const char* referrer_url;
  };
  constexpr TestCase kTestCases[] = {
      {"ReferrerSubdomain",
       "https://plugin.afferdmail.com/crx/final-extension.crx",
       "https://plugin.afferdmail.com/crx/source-extension.crx",
       "https://sub.plugin.afferdmail.com/crx/extension-details"},
      {"ReferrerSimilarOrigin",
       "https://plugin.afferdmail.com/crx/final-extension.crx",
       "https://plugin.afferdmail.com/crx/source-extension.crx",
       "https://plugin.afferdmail.com.evil.test/crx/extension-details"},
      {"FinalWrongDirectory",
       "https://plugin.afferdmail.com/downloads/final-extension.crx",
       "https://plugin.afferdmail.com/crx/source-extension.crx",
       "https://plugin.afferdmail.com/crx/extension-details"},
      {"OriginalWrongSuffix",
       "https://plugin.afferdmail.com/crx/final-extension.crx",
       "https://plugin.afferdmail.com/crx/source-extension.zip",
       "https://plugin.afferdmail.com/crx/extension-details"},
      {"ReferrerWrongDirectory",
       "https://plugin.afferdmail.com/crx/final-extension.crx",
       "https://plugin.afferdmail.com/crx/source-extension.crx",
       "https://plugin.afferdmail.com/store/extension-details"},
      {"QueryDisguisedCrx",
       "https://plugin.afferdmail.com/crx/final-extension.zip?file=.crx",
       "https://plugin.afferdmail.com/crx/source-extension.crx",
       "https://plugin.afferdmail.com/crx/extension-details"},
      {"EncodedTraversal",
       "https://plugin.afferdmail.com/crx/%2e%2e/final-extension.crx",
       "https://plugin.afferdmail.com/crx/source-extension.crx",
       "https://plugin.afferdmail.com/crx/extension-details"},
      {"EncodedPathSeparator",
       "https://plugin.afferdmail.com/crx/folder%2ffinal-extension.crx",
       "https://plugin.afferdmail.com/crx/source-extension.crx",
       "https://plugin.afferdmail.com/crx/extension-details"},
  };

  for (const auto& test_case : kTestCases) {
    SCOPED_TRACE(test_case.name);
    DownloadItemData data = MakeValidDownloadItemData();
    data.final_url = GURL(test_case.final_url);
    data.original_url = GURL(test_case.original_url);
    data.referrer_url = GURL(test_case.referrer_url);
    data.url_chain = {data.original_url, data.final_url};
    StrictMockDownloadItem download_item;
    ExpectDownloadItem(download_item, data);

    EXPECT_FALSE(IsBraveInternalExtensionStoreDownload(download_item));
  }
}

TEST(InternalExtensionStoreDownloadTest, RejectsUnsafeRedirects) {
  struct TestCase {
    const char* name;
    const char* original_url;
    const char* final_url;
    std::vector<GURL> url_chain;
  };
  const TestCase kTestCases[] = {
      {"CrossOriginOriginal",
       "https://evil.test/crx/source-extension.crx",
       "https://plugin.afferdmail.com/crx/final-extension.crx",
       {GURL("https://evil.test/crx/source-extension.crx"),
        GURL("https://plugin.afferdmail.com/crx/final-extension.crx")}},
      {"CrossOriginFinal",
       "https://plugin.afferdmail.com/crx/source-extension.crx",
       "https://evil.test/crx/final-extension.crx",
       {GURL("https://plugin.afferdmail.com/crx/source-extension.crx"),
        GURL("https://evil.test/crx/final-extension.crx")}},
      {"CrossOriginIntermediate",
       "https://plugin.afferdmail.com/crx/source-extension.crx",
       "https://plugin.afferdmail.com/crx/final-extension.crx",
       {GURL("https://plugin.afferdmail.com/crx/source-extension.crx"),
        GURL("https://evil.test/crx/intermediate.crx"),
        GURL("https://plugin.afferdmail.com/crx/final-extension.crx")}},
      {"HttpDowngrade",
       "https://plugin.afferdmail.com/crx/source-extension.crx",
       "http://plugin.afferdmail.com/crx/final-extension.crx",
       {GURL("https://plugin.afferdmail.com/crx/source-extension.crx"),
        GURL("http://plugin.afferdmail.com/crx/final-extension.crx")}},
      {"HttpOriginal",
       "http://plugin.afferdmail.com/crx/source-extension.crx",
       "https://plugin.afferdmail.com/crx/final-extension.crx",
       {GURL("http://plugin.afferdmail.com/crx/source-extension.crx"),
        GURL("https://plugin.afferdmail.com/crx/final-extension.crx")}},
  };

  for (const auto& test_case : kTestCases) {
    SCOPED_TRACE(test_case.name);
    DownloadItemData data = MakeValidDownloadItemData();
    data.original_url = GURL(test_case.original_url);
    data.final_url = GURL(test_case.final_url);
    data.url_chain = test_case.url_chain;
    StrictMockDownloadItem download_item;
    ExpectDownloadItem(download_item, data);

    EXPECT_FALSE(IsBraveInternalExtensionStoreDownload(download_item));
  }
}

TEST(InternalExtensionStoreUpdateUrlTest, AllowsExactStoreUpdatePath) {
  EXPECT_TRUE(IsBraveInternalExtensionStoreUpdateUrl(
      GURL("https://plugin.afferdmail.com/crx/extension-id/update.xml")));
}

TEST(InternalExtensionStoreUpdateUrlTest, RejectsInvalidUrls) {
  struct TestCase {
    const char* name;
    const char* url;
  };
  constexpr TestCase kTestCases[] = {
      {"Missing", ""},
      {"Http", "http://plugin.afferdmail.com/crx/extension-id/update.xml"},
      {"OtherOrigin", "https://evil.test/crx/extension-id/update.xml"},
      {"Subdomain",
       "https://sub.plugin.afferdmail.com/crx/extension-id/update.xml"},
      {"SimilarOrigin",
       "https://plugin.afferdmail.com.evil.test/crx/extension-id/update.xml"},
      {"WrongDirectory",
       "https://plugin.afferdmail.com/extensions/extension-id/update.xml"},
      {"WrongSuffix",
       "https://plugin.afferdmail.com/crx/extension-id/update.json"},
      {"SuffixExtension",
       "https://plugin.afferdmail.com/crx/extension-id/update.xml.bak"},
      {"WrongCase",
       "https://plugin.afferdmail.com/crx/extension-id/UPDATE.XML"},
      {"EmptyExtensionSegment",
       "https://plugin.afferdmail.com/crx//update.xml"},
      {"QueryDisguise",
       "https://plugin.afferdmail.com/crx/extension-id?path=/update.xml"},
      {"EncodedTraversal",
       "https://plugin.afferdmail.com/crx/%2e%2e/extension-id/update.xml"},
      {"EncodedPathSeparator",
       "https://plugin.afferdmail.com/crx/extension-id%2fchild/update.xml"},
  };

  for (const auto& test_case : kTestCases) {
    SCOPED_TRACE(test_case.name);
    EXPECT_FALSE(IsBraveInternalExtensionStoreUpdateUrl(GURL(test_case.url)));
  }
}

TEST_F(InternalExtensionStoreIntegrationTest,
       PreservesOfficialWebStoreClassification) {
  DownloadItemData data = MakeValidDownloadItemData();
  data.final_url =
      GURL("https://clients2.google.com/service/update2/crx?response=redirect");
  data.original_url = data.final_url;
  data.referrer_url = GURL("https://chromewebstore.google.com/detail/test");
  data.url_chain = {data.final_url};
  data.mime_type = Extension::kMimeType;
  StrictMockDownloadItem download_item;
  ExpectDownloadItem(download_item, data);
  TestingProfile profile;

  EXPECT_TRUE(IsExtensionDownload(download_item));
  EXPECT_FALSE(IsBraveInternalExtensionStoreDownload(download_item));
  EXPECT_TRUE(
      download_crx_util::IsTrustedExtensionDownload(&profile, download_item));
  download_crx_util::SetMockInstallPromptForTesting(
      std::make_unique<ExtensionInstallPrompt>(&profile, gfx::NativeWindow()));
  EXPECT_EQ(CrxInstaller::OffStoreInstallDisallowed,
            download_crx_util::CreateCrxInstaller(&profile, download_item)
                ->off_store_install_allow_reason());
}

TEST_F(InternalExtensionStoreIntegrationTest,
       PreservesPolicyApprovedOffStorePath) {
  DownloadItemData data = MakeValidDownloadItemData();
  data.final_url = GURL("https://extensions.example/download/extension.crx");
  data.original_url = data.final_url;
  data.referrer_url = GURL("https://extensions.example/catalog");
  data.url_chain = {data.final_url};
  data.mime_type = Extension::kMimeType;
  StrictMockDownloadItem download_item;
  ExpectDownloadItem(download_item, data);
  TestingProfile profile;
  auto policy_override =
      download_crx_util::OverrideOffstoreInstallAllowedForTesting(true);
  ASSERT_TRUE(policy_override);

  EXPECT_TRUE(IsExtensionDownload(download_item));
  EXPECT_FALSE(IsBraveInternalExtensionStoreDownload(download_item));
  EXPECT_TRUE(
      download_crx_util::IsTrustedExtensionDownload(&profile, download_item));
}

TEST(InternalExtensionStoreDownloadTest,
     LeavesUnrelatedGenericMimeDownloadUnchanged) {
  DownloadItemData data = MakeValidDownloadItemData();
  data.final_url = GURL("https://downloads.example/extension.crx");
  data.original_url = data.final_url;
  data.referrer_url = GURL("https://downloads.example/catalog");
  data.url_chain = {data.final_url};
  data.mime_type = "application/octet-stream";
  StrictMockDownloadItem download_item;
  ExpectDownloadItem(download_item, data);

  EXPECT_FALSE(IsBraveInternalExtensionStoreDownload(download_item));
  EXPECT_FALSE(IsExtensionDownload(download_item));
}

TEST_F(InternalExtensionStoreIntegrationTest,
       InternalStoreReasonAllowsRegularProfile) {
  const DownloadItemData data = MakeValidDownloadItemData();
  StrictMockDownloadItem download_item;
  ExpectDownloadItem(download_item, data);
  TestingProfile profile;

  EXPECT_TRUE(download_crx_util::IsBraveInternalExtensionStoreInstallAllowed(
      &profile, download_item));
  download_crx_util::SetMockInstallPromptForTesting(
      std::make_unique<ExtensionInstallPrompt>(&profile, gfx::NativeWindow()));
  EXPECT_EQ(CrxInstaller::OffStoreInstallAllowedFromBraveInternalStore,
            download_crx_util::CreateCrxInstaller(&profile, download_item)
                ->off_store_install_allow_reason());
}

TEST_F(InternalExtensionStoreIntegrationTest,
       InternalStoreRejectsIncognitoProfile) {
  const DownloadItemData data = MakeValidDownloadItemData();
  StrictMockDownloadItem download_item;
  ExpectDownloadItem(download_item, data);
  TestingProfile profile;

  auto policy_override =
      download_crx_util::OverrideOffstoreInstallAllowedForTesting(true);
  ASSERT_TRUE(policy_override);

  Profile* otr_profile =
      profile.GetPrimaryOTRProfile(/*create_if_needed=*/true);
  EXPECT_FALSE(download_crx_util::IsBraveInternalExtensionStoreInstallAllowed(
      otr_profile, download_item));
  EXPECT_FALSE(download_crx_util::IsTrustedExtensionDownload(otr_profile,
                                                             download_item));

  profile.DestroyOffTheRecordProfile(otr_profile);
}

TEST_F(InternalExtensionStoreIntegrationTest,
       InternalStoreRejectsGuestProfile) {
  const DownloadItemData data = MakeValidDownloadItemData();
  StrictMockDownloadItem download_item;
  ExpectDownloadItem(download_item, data);

  TestingProfile::Builder guest_builder;
  std::unique_ptr<TestingProfile> guest_profile =
      guest_builder.SetGuestSession().Build();
  EXPECT_FALSE(download_crx_util::IsBraveInternalExtensionStoreInstallAllowed(
      guest_profile.get(), download_item));
  EXPECT_FALSE(download_crx_util::IsTrustedExtensionDownload(
      guest_profile.get(), download_item));
}

TEST_F(InternalExtensionStoreIntegrationTest,
       InternalStoreRejectsSystemProfile) {
  const DownloadItemData data = MakeValidDownloadItemData();
  StrictMockDownloadItem download_item;
  ExpectDownloadItem(download_item, data);

  TestingProfileManager profile_manager(TestingBrowserProcess::GetGlobal());
  ASSERT_TRUE(profile_manager.SetUp());
  TestingProfile* system_profile = profile_manager.CreateSystemProfile();
  EXPECT_FALSE(download_crx_util::IsBraveInternalExtensionStoreInstallAllowed(
      system_profile, download_item));
  EXPECT_FALSE(download_crx_util::IsTrustedExtensionDownload(system_profile,
                                                             download_item));
}

TEST_F(InternalExtensionStoreIntegrationTest,
       InternalStoreRejectsTorProfile) {
  const DownloadItemData data = MakeValidDownloadItemData();
  StrictMockDownloadItem download_item;
  ExpectDownloadItem(download_item, data);
  TestingProfile profile;

  Profile* tor_profile = profile.GetOffTheRecordProfile(
      Profile::OTRProfileID::TorID(), /*create_if_needed=*/true);
  ASSERT_TRUE(tor_profile->IsTor());
  EXPECT_FALSE(download_crx_util::IsBraveInternalExtensionStoreInstallAllowed(
      tor_profile, download_item));
  EXPECT_FALSE(download_crx_util::IsTrustedExtensionDownload(tor_profile,
                                                             download_item));

  profile.DestroyOffTheRecordProfile(tor_profile);
}

}  // namespace

}  // namespace extensions::util
