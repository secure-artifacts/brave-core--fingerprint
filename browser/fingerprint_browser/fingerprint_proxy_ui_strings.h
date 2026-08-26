/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_FINGERPRINT_BROWSER_FINGERPRINT_PROXY_UI_STRINGS_H_
#define BRAVE_BROWSER_FINGERPRINT_BROWSER_FINGERPRINT_PROXY_UI_STRINGS_H_

#include <string>
#include <string_view>

#include "brave/components/fingerprint_browser/browser/profile_proxy_config.h"

class PrefService;

namespace fingerprint_browser {

inline constexpr char kProxyMessageNone[] = "none";
inline constexpr char kProxyMessageWaiting[] = "waiting_for_verification";
inline constexpr char kProxyMessageChecking[] = "checking";
inline constexpr char kProxyMessageActive[] = "active";
inline constexpr char kProxyMessageRevalidationRetrying[] =
    "revalidation_retrying";
inline constexpr char kProxyMessageAwaitingConfirmation[] =
    "awaiting_confirmation";
inline constexpr char kProxyMessageControlChanged[] = "control_changed";
inline constexpr char kProxyMessageNoActiveProxy[] = "no_active_proxy";
inline constexpr char kProxyMessagePasswordRequired[] = "password_required";
inline constexpr char kProxyMessageCredentialsUnavailable[] =
    "credentials_unavailable";
inline constexpr char kProxyMessageCredentialEncryptionFailed[] =
    "credential_encryption_failed";
inline constexpr char kProxyMessageInvalidConfig[] = "invalid_config";
inline constexpr char kProxyMessagePolicyConflict[] = "policy_conflict";
inline constexpr char kProxyMessageExtensionConflict[] = "extension_conflict";
inline constexpr char kProxyMessageVerificationMissing[] =
    "verification_missing";
inline constexpr char kProxyMessageVerificationExpired[] =
    "verification_expired";
inline constexpr char kProxyMessageVerificationCancelled[] =
    "verification_cancelled";
inline constexpr char kProxyMessageVerificationBusy[] = "verification_busy";
inline constexpr char kProxyMessageConnectionFailed[] = "connection_failed";
inline constexpr char kProxyMessageGeoUnavailable[] = "geo_unavailable";
inline constexpr char kProxyMessageLanguageUnavailable[] =
    "language_unavailable";
inline constexpr char kProxyMessageProfileUnavailable[] = "profile_unavailable";
inline constexpr char kProxyMessageUnknown[] = "unknown";

inline constexpr char kProxyWarningNone[] = "none";
inline constexpr char kProxyWarningCountryChanged[] = "country_changed";
inline constexpr char kProxyWarningIpChanged[] = "ip_changed";
inline constexpr char kProxyWarningUnknown[] = "unknown_warning";

std::string_view NormalizeProxyStatusCode(std::string_view value);
std::string_view NormalizeProxyWarningCode(std::string_view value);
std::string_view ProxyConflictMessageCode(ProfileProxyConfigConflict conflict);
std::u16string GetProxyUiMessage(std::string_view code, int net_error = 0);
std::string GetChineseCountryName(std::string_view country_code,
                                  std::string_view fallback);
void MigrateProxyUiMessagePrefs(PrefService& pref_service);

}  // namespace fingerprint_browser

#endif  // BRAVE_BROWSER_FINGERPRINT_BROWSER_FINGERPRINT_PROXY_UI_STRINGS_H_
