/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/fingerprint_browser/fingerprint_proxy_ui_strings.h"

#include <array>
#include <string_view>
#include <utility>

#include "brave/components/fingerprint_browser/browser/pref_names.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace fingerprint_browser {
namespace {

using CodePair = std::pair<std::string_view, std::string_view>;

bool ContainsChinese(std::u16string_view value) {
  for (const char16_t character : value) {
    if (character >= 0x3400 && character <= 0x9fff) {
      return true;
    }
  }
  return false;
}

constexpr std::array kLegacyStatuses = {
    CodePair{"", kProxyMessageNone},
    CodePair{"Proxy is waiting for verification.", kProxyMessageWaiting},
    CodePair{"Checking proxy exit location.", kProxyMessageChecking},
    CodePair{"Proxy is active.", kProxyMessageActive},
    CodePair{"Proxy verified. Confirm to apply it.",
             kProxyMessageAwaitingConfirmation},
    CodePair{"Proxy control changed. Revalidating.",
             kProxyMessageControlChanged},
    CodePair{"No active proxy is configured.", kProxyMessageNoActiveProxy},
    CodePair{"Enter the proxy password again after changing proxy details.",
             kProxyMessagePasswordRequired},
    CodePair{"Saved proxy credentials could not be unlocked.",
             kProxyMessageCredentialsUnavailable},
    CodePair{"Saved proxy credentials are unavailable.",
             kProxyMessageCredentialsUnavailable},
    CodePair{"Saved proxy credentials could not be encrypted.",
             kProxyMessageCredentialEncryptionFailed},
    CodePair{"Proxy credentials could not be encrypted.",
             kProxyMessageCredentialEncryptionFailed},
    CodePair{"Enter a valid proxy protocol, host, and port.",
             kProxyMessageInvalidConfig},
    CodePair{
        "Proxy settings are controlled by policy. Profile proxy is disabled.",
        kProxyMessagePolicyConflict},
    CodePair{"Proxy settings are controlled by an extension. Profile proxy "
             "is disabled.",
             kProxyMessageExtensionConflict},
    CodePair{"Verification is missing or was already used.",
             kProxyMessageVerificationMissing},
    CodePair{"Verification expired. Verify the proxy again.",
             kProxyMessageVerificationExpired},
    CodePair{"Proxy verification was cancelled.",
             kProxyMessageVerificationCancelled},
    CodePair{"Another proxy verification is already running.",
             kProxyMessageVerificationBusy},
    CodePair{"Proxy connection or authentication failed.",
             kProxyMessageConnectionFailed},
    CodePair{"Proxy location services are temporarily unavailable.",
             kProxyMessageGeoUnavailable},
    CodePair{"Proxy country could not be mapped to a language.",
             kProxyMessageLanguageUnavailable},
    CodePair{"Profile proxy is unavailable in this window.",
             kProxyMessageProfileUnavailable},
};

constexpr std::array kStableStatuses = {
    std::string_view(kProxyMessageWaiting),
    std::string_view(kProxyMessageChecking),
    std::string_view(kProxyMessageActive),
    std::string_view(kProxyMessageAwaitingConfirmation),
    std::string_view(kProxyMessageControlChanged),
    std::string_view(kProxyMessageNoActiveProxy),
    std::string_view(kProxyMessagePasswordRequired),
    std::string_view(kProxyMessageCredentialsUnavailable),
    std::string_view(kProxyMessageCredentialEncryptionFailed),
    std::string_view(kProxyMessageInvalidConfig),
    std::string_view(kProxyMessagePolicyConflict),
    std::string_view(kProxyMessageExtensionConflict),
    std::string_view(kProxyMessageVerificationMissing),
    std::string_view(kProxyMessageVerificationExpired),
    std::string_view(kProxyMessageVerificationCancelled),
    std::string_view(kProxyMessageVerificationBusy),
    std::string_view(kProxyMessageConnectionFailed),
    std::string_view(kProxyMessageGeoUnavailable),
    std::string_view(kProxyMessageLanguageUnavailable),
    std::string_view(kProxyMessageProfileUnavailable),
    std::string_view(kProxyMessageUnknown),
};

}  // namespace

TEST(FingerprintProxyUiStringsTest, MigratesLegacyStatusMessages) {
  for (const auto& [legacy, code] : kLegacyStatuses) {
    EXPECT_EQ(NormalizeProxyStatusCode(legacy), code) << legacy;
  }
  EXPECT_EQ(NormalizeProxyStatusCode("unrecognized legacy text"),
            kProxyMessageUnknown);
}

TEST(FingerprintProxyUiStringsTest, MigratesLegacyWarnings) {
  EXPECT_EQ(NormalizeProxyWarningCode(
                "Proxy country changed. Fingerprint settings updated."),
            kProxyWarningCountryChanged);
  EXPECT_EQ(NormalizeProxyWarningCode("Proxy exit IP changed."),
            kProxyWarningIpChanged);
  EXPECT_EQ(NormalizeProxyWarningCode("unrecognized legacy text"),
            kProxyWarningUnknown);
}

TEST(FingerprintProxyUiStringsTest, KeepsStableCodes) {
  for (const auto code : kStableStatuses) {
    EXPECT_EQ(NormalizeProxyStatusCode(code), code) << code;
  }
  EXPECT_EQ(NormalizeProxyWarningCode(kProxyWarningIpChanged),
            kProxyWarningIpChanged);
}

TEST(FingerprintProxyUiStringsTest, EveryCodeHasChineseUiMessage) {
  for (const auto code : kStableStatuses) {
    EXPECT_TRUE(ContainsChinese(GetProxyUiMessage(code))) << code;
  }
  EXPECT_TRUE(ContainsChinese(GetProxyUiMessage(kProxyWarningCountryChanged)));
  EXPECT_TRUE(ContainsChinese(GetProxyUiMessage(kProxyWarningIpChanged)));
  EXPECT_TRUE(ContainsChinese(GetProxyUiMessage(kProxyWarningUnknown)));
  EXPECT_EQ(GetProxyUiMessage(kProxyMessageUnknown), u"代理操作失败。");
  EXPECT_EQ(GetProxyUiMessage(kProxyWarningUnknown),
            u"代理状态发生变化，请立即复检。");
  EXPECT_TRUE(ContainsChinese(GetProxyUiMessage("not_registered", -130)));
  EXPECT_NE(GetProxyUiMessage("not_registered", -130).find(u"-130"),
            std::u16string::npos);
}

TEST(FingerprintProxyUiStringsTest, MigratesStoredPrefsToStableCodes) {
  TestingPrefServiceSimple pref_service;
  pref_service.registry()->RegisterStringPref(prefs::kProfileProxyStatusMessage,
                                              std::string());
  pref_service.registry()->RegisterStringPref(prefs::kProfileProxyChangeWarning,
                                              std::string());
  pref_service.SetString(prefs::kProfileProxyStatusMessage, "Proxy is active.");
  pref_service.SetString(prefs::kProfileProxyChangeWarning,
                         "Proxy exit IP changed.");

  MigrateProxyUiMessagePrefs(pref_service);

  EXPECT_EQ(pref_service.GetString(prefs::kProfileProxyStatusMessage),
            kProxyMessageActive);
  EXPECT_EQ(pref_service.GetString(prefs::kProfileProxyChangeWarning),
            kProxyWarningIpChanged);

  pref_service.SetString(prefs::kProfileProxyStatusMessage, "legacy unknown");
  pref_service.SetString(prefs::kProfileProxyChangeWarning, "legacy unknown");
  MigrateProxyUiMessagePrefs(pref_service);
  EXPECT_EQ(pref_service.GetString(prefs::kProfileProxyStatusMessage),
            kProxyMessageUnknown);
  EXPECT_EQ(pref_service.GetString(prefs::kProfileProxyChangeWarning),
            kProxyWarningUnknown);
}

TEST(FingerprintProxyUiStringsTest, MapsConflictsAndCountryNames) {
  EXPECT_EQ(ProxyConflictMessageCode(ProfileProxyConfigConflict::kNone),
            kProxyMessageNone);
  EXPECT_EQ(ProxyConflictMessageCode(ProfileProxyConfigConflict::kPolicy),
            kProxyMessagePolicyConflict);
  EXPECT_EQ(ProxyConflictMessageCode(ProfileProxyConfigConflict::kExtension),
            kProxyMessageExtensionConflict);
  EXPECT_EQ(GetChineseCountryName("US", "fallback"), "美国");
  EXPECT_EQ(GetChineseCountryName("invalid", "后备值"), "后备值");
}

}  // namespace fingerprint_browser
