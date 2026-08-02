/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_FINGERPRINT_BROWSER_BROWSER_PROFILE_PROXY_CONFIG_H_
#define BRAVE_COMPONENTS_FINGERPRINT_BROWSER_BROWSER_PROFILE_PROXY_CONFIG_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "net/base/proxy_server.h"

class PrefService;

namespace fingerprint_browser {

enum class ProfileProxyConfigConflict {
  kNone,
  kPolicy,
  kExtension,
};

struct ProfileProxyGeo {
  std::string country_code;
  std::string country_name;
  std::string region_name;
  std::string city_name;
  std::string timezone;
  double latitude = 0.0;
  double longitude = 0.0;
  std::string accept_languages;
};

struct ProfileProxyDraft {
  std::string scheme;
  std::string host;
  int port = 0;
  std::string username;
  std::string password;
};

bool IsProfileProxyEnabled(const PrefService& prefs);
std::optional<net::ProxyServer> BuildProfileProxyServer(
    const ProfileProxyDraft& draft);
std::optional<net::ProxyServer> GetProfileProxyServerFromPrefs(
    const PrefService& prefs);
ProfileProxyConfigConflict GetProfileProxyConfigConflict(
    const PrefService& prefs);
bool ShouldUseProfileProxy(const PrefService& prefs);
std::string_view ProfileProxyConfigConflictWarning(
    ProfileProxyConfigConflict conflict);
void SyncProfileProxyWebRTCPolicy(PrefService& prefs);
void SyncProfileProxyLanguage(PrefService& prefs);
void SyncProfileProxyDerivedPrefs(PrefService& prefs);
void ApplyVerifiedProfileProxyGeo(PrefService& prefs,
                                  const ProfileProxyGeo& geo);
void PrepareVerifiedProfileProxyDerivedPrefs(PrefService& prefs,
                                             const ProfileProxyGeo& geo);
void ClearVerifiedProfileProxyGeo(PrefService& prefs);
std::optional<std::string> AcceptLanguagesForCountryCode(
    std::string_view country_code);
std::optional<std::string> GetProfileProxyAcceptLanguagesForPrefs(
    const PrefService& prefs);
std::optional<ProfileProxyGeo> GetProfileProxyGeoForPrefs(
    const PrefService& prefs);
std::vector<std::string> GetProfileProxyLanguagesForPrefs(
    const PrefService& prefs,
    const std::vector<std::string>& fallback_languages);
std::string_view ProfileProxyGeoWarning(const PrefService& prefs);
void SetProfileProxyLastError(PrefService& prefs, int net_error);
void ClearProfileProxyLastError(PrefService& prefs);

}  // namespace fingerprint_browser

#endif  // BRAVE_COMPONENTS_FINGERPRINT_BROWSER_BROWSER_PROFILE_PROXY_CONFIG_H_
