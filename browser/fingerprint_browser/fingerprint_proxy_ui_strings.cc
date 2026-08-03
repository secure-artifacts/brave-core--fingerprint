/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/fingerprint_browser/fingerprint_proxy_ui_strings.h"

#include <array>
#include <utility>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "brave/components/fingerprint_browser/browser/pref_names.h"
#include "brave/grit/brave_generated_resources.h"
#include "components/prefs/pref_service.h"
#include "ui/base/l10n/l10n_util.h"
#include "unicode/locid.h"
#include "unicode/unistr.h"

namespace fingerprint_browser {
namespace {

using LegacyCode = std::pair<std::string_view, std::string_view>;

constexpr std::array kLegacyStatuses = {
    LegacyCode{"", kProxyMessageNone},
    LegacyCode{"Proxy is waiting for verification.", kProxyMessageWaiting},
    LegacyCode{"Checking proxy exit location.", kProxyMessageChecking},
    LegacyCode{"Proxy is active.", kProxyMessageActive},
    LegacyCode{"Proxy verified. Confirm to apply it.",
               kProxyMessageAwaitingConfirmation},
    LegacyCode{"Proxy control changed. Revalidating.",
               kProxyMessageControlChanged},
    LegacyCode{"No active proxy is configured.", kProxyMessageNoActiveProxy},
    LegacyCode{"Enter the proxy password again after changing proxy details.",
               kProxyMessagePasswordRequired},
    LegacyCode{"Saved proxy credentials could not be unlocked.",
               kProxyMessageCredentialsUnavailable},
    LegacyCode{"Saved proxy credentials are unavailable.",
               kProxyMessageCredentialsUnavailable},
    LegacyCode{"Saved proxy credentials could not be encrypted.",
               kProxyMessageCredentialEncryptionFailed},
    LegacyCode{"Proxy credentials could not be encrypted.",
               kProxyMessageCredentialEncryptionFailed},
    LegacyCode{"Enter a valid proxy protocol, host, and port.",
               kProxyMessageInvalidConfig},
    LegacyCode{
        "Proxy settings are controlled by policy. Profile proxy is disabled.",
        kProxyMessagePolicyConflict},
    LegacyCode{"Proxy settings are controlled by an extension. Profile proxy "
               "is disabled.",
               kProxyMessageExtensionConflict},
    LegacyCode{"Verification is missing or was already used.",
               kProxyMessageVerificationMissing},
    LegacyCode{"Verification expired. Verify the proxy again.",
               kProxyMessageVerificationExpired},
    LegacyCode{"Proxy verification was cancelled.",
               kProxyMessageVerificationCancelled},
    LegacyCode{"Another proxy verification is already running.",
               kProxyMessageVerificationBusy},
    LegacyCode{"Proxy connection or authentication failed.",
               kProxyMessageConnectionFailed},
    LegacyCode{"Proxy location services are temporarily unavailable.",
               kProxyMessageGeoUnavailable},
    LegacyCode{"Proxy country could not be mapped to a language.",
               kProxyMessageLanguageUnavailable},
    LegacyCode{"Profile proxy is unavailable in this window.",
               kProxyMessageProfileUnavailable},
};

constexpr std::array kStableStatuses = {
    std::string_view(kProxyMessageNone),
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

int ResourceIdForCode(std::string_view code) {
  if (code == kProxyMessageWaiting) {
    return IDS_FINGERPRINT_PROXY_STATUS_WAITING;
  }
  if (code == kProxyMessageChecking) {
    return IDS_FINGERPRINT_PROXY_STATUS_CHECKING;
  }
  if (code == kProxyMessageActive) {
    return IDS_FINGERPRINT_PROXY_STATUS_ACTIVE;
  }
  if (code == kProxyMessageAwaitingConfirmation) {
    return IDS_FINGERPRINT_PROXY_STATUS_AWAITING_CONFIRMATION;
  }
  if (code == kProxyMessageControlChanged) {
    return IDS_FINGERPRINT_PROXY_STATUS_CONTROL_CHANGED;
  }
  if (code == kProxyMessageNoActiveProxy) {
    return IDS_FINGERPRINT_PROXY_STATUS_NO_ACTIVE_PROXY;
  }
  if (code == kProxyMessagePasswordRequired) {
    return IDS_FINGERPRINT_PROXY_ERROR_PASSWORD_REQUIRED;
  }
  if (code == kProxyMessageCredentialsUnavailable) {
    return IDS_FINGERPRINT_PROXY_ERROR_CREDENTIALS_UNAVAILABLE;
  }
  if (code == kProxyMessageCredentialEncryptionFailed) {
    return IDS_FINGERPRINT_PROXY_ERROR_CREDENTIALS_ENCRYPT;
  }
  if (code == kProxyMessageInvalidConfig) {
    return IDS_FINGERPRINT_PROXY_ERROR_INVALID_CONFIG;
  }
  if (code == kProxyMessagePolicyConflict) {
    return IDS_FINGERPRINT_PROXY_ERROR_POLICY_CONFLICT;
  }
  if (code == kProxyMessageExtensionConflict) {
    return IDS_FINGERPRINT_PROXY_ERROR_EXTENSION_CONFLICT;
  }
  if (code == kProxyMessageVerificationMissing) {
    return IDS_FINGERPRINT_PROXY_ERROR_VERIFICATION_MISSING;
  }
  if (code == kProxyMessageVerificationExpired) {
    return IDS_FINGERPRINT_PROXY_ERROR_VERIFICATION_EXPIRED;
  }
  if (code == kProxyMessageVerificationCancelled) {
    return IDS_FINGERPRINT_PROXY_ERROR_VERIFICATION_CANCELLED;
  }
  if (code == kProxyMessageVerificationBusy) {
    return IDS_FINGERPRINT_PROXY_ERROR_VERIFICATION_BUSY;
  }
  if (code == kProxyMessageConnectionFailed) {
    return IDS_FINGERPRINT_PROXY_ERROR_CONNECTION;
  }
  if (code == kProxyMessageGeoUnavailable) {
    return IDS_FINGERPRINT_PROXY_ERROR_GEO;
  }
  if (code == kProxyMessageLanguageUnavailable) {
    return IDS_FINGERPRINT_PROXY_ERROR_LANGUAGE;
  }
  if (code == kProxyMessageProfileUnavailable) {
    return IDS_FINGERPRINT_PROXY_ERROR_PROFILE_UNAVAILABLE;
  }
  if (code == kProxyWarningCountryChanged) {
    return IDS_FINGERPRINT_PROXY_WARNING_COUNTRY_CHANGED;
  }
  if (code == kProxyWarningIpChanged) {
    return IDS_FINGERPRINT_PROXY_WARNING_IP_CHANGED;
  }
  if (code == kProxyWarningUnknown) {
    return IDS_FINGERPRINT_PROXY_WARNING_UNKNOWN;
  }
  return IDS_FINGERPRINT_PROXY_ERROR_UNKNOWN;
}

}  // namespace

std::string_view NormalizeProxyStatusCode(std::string_view value) {
  for (const auto code : kStableStatuses) {
    if (value == code) {
      return code;
    }
  }
  for (const auto& [legacy, code] : kLegacyStatuses) {
    if (value == legacy) {
      return code;
    }
  }
  return kProxyMessageUnknown;
}

std::string_view NormalizeProxyWarningCode(std::string_view value) {
  if (value.empty() || value == kProxyWarningNone) {
    return kProxyWarningNone;
  }
  if (value == kProxyWarningCountryChanged ||
      value == "Proxy country changed. Fingerprint settings updated.") {
    return kProxyWarningCountryChanged;
  }
  if (value == kProxyWarningIpChanged || value == "Proxy exit IP changed.") {
    return kProxyWarningIpChanged;
  }
  if (value == kProxyWarningUnknown) {
    return kProxyWarningUnknown;
  }
  return kProxyWarningUnknown;
}

std::string_view ProxyConflictMessageCode(ProfileProxyConfigConflict conflict) {
  switch (conflict) {
    case ProfileProxyConfigConflict::kNone:
      return kProxyMessageNone;
    case ProfileProxyConfigConflict::kPolicy:
      return kProxyMessagePolicyConflict;
    case ProfileProxyConfigConflict::kExtension:
      return kProxyMessageExtensionConflict;
  }
  return kProxyMessageUnknown;
}

std::u16string GetProxyUiMessage(std::string_view code, int net_error) {
  if (code.empty() || code == kProxyMessageNone || code == kProxyWarningNone) {
    return std::u16string();
  }
  std::u16string message = l10n_util::GetStringUTF16(ResourceIdForCode(code));
  if (net_error != 0) {
    base::StrAppend(
        &message, {u"（网络错误：", base::NumberToString16(net_error), u"）"});
  }
  return message;
}

std::string GetChineseCountryName(std::string_view country_code,
                                  std::string_view fallback) {
  if (country_code.size() != 2) {
    return std::string(fallback);
  }
  const icu::Locale country_locale("", std::string(country_code).c_str());
  const icu::Locale chinese_locale("zh", "CN");
  icu::UnicodeString display_name;
  country_locale.getDisplayCountry(chinese_locale, display_name);
  std::string value;
  display_name.toUTF8String(value);
  return value.empty() ? std::string(fallback) : value;
}

void MigrateProxyUiMessagePrefs(PrefService& pref_service) {
  pref_service.SetString(prefs::kProfileProxyStatusMessage,
                         NormalizeProxyStatusCode(pref_service.GetString(
                             prefs::kProfileProxyStatusMessage)));
  pref_service.SetString(prefs::kProfileProxyChangeWarning,
                         NormalizeProxyWarningCode(pref_service.GetString(
                             prefs::kProfileProxyChangeWarning)));
}

}  // namespace fingerprint_browser
