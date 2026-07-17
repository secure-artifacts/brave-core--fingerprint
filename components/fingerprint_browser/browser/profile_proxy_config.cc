/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/fingerprint_browser/browser/profile_proxy_config.h"

#include <cstdint>
#include <string>

#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "brave/components/fingerprint_browser/browser/offline_geoip_database.h"
#include "brave/components/fingerprint_browser/browser/pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/proxy_config/proxy_config_pref_names.h"
#include "net/base/host_port_pair.h"
#include "net/base/ip_address.h"
#include "net/base/net_errors.h"

namespace fingerprint_browser {

namespace {

constexpr char kWebRTCIPHandlingPolicyPref[] = "webrtc.ip_handling_policy";
constexpr char kWebRTCIPHandlingDisableNonProxiedUdp[] =
    "disable_non_proxied_udp";
constexpr char kAcceptLanguagesPref[] = "intl.accept_languages";
constexpr char kGeoLookupWarning[] =
    "Proxy IP could not be located. Enter manual country, time zone, "
    "latitude, and longitude to avoid host geo leakage.";

std::optional<net::ProxyServer::Scheme> SchemeFromPref(
    std::string_view scheme) {
  if (scheme == prefs::kProfileProxySchemeHttp) {
    return net::ProxyServer::SCHEME_HTTP;
  }
  if (scheme == prefs::kProfileProxySchemeSocks5) {
    return net::ProxyServer::SCHEME_SOCKS5;
  }
  return std::nullopt;
}

void ApplyProfileProxyWebRTCPolicy(PrefService& prefs) {
  if (!prefs.FindPreference(kWebRTCIPHandlingPolicyPref)) {
    return;
  }
  if (!prefs.GetBoolean(prefs::kProfileProxyHasSavedWebRTCIPHandlingPolicy)) {
    prefs.SetString(prefs::kProfileProxySavedWebRTCIPHandlingPolicy,
                    prefs.GetString(kWebRTCIPHandlingPolicyPref));
    prefs.SetBoolean(prefs::kProfileProxyHasSavedWebRTCIPHandlingPolicy, true);
  }
  prefs.SetString(kWebRTCIPHandlingPolicyPref,
                  kWebRTCIPHandlingDisableNonProxiedUdp);
}

void RestoreProfileProxyWebRTCPolicy(PrefService& prefs) {
  if (!prefs.FindPreference(kWebRTCIPHandlingPolicyPref) ||
      !prefs.GetBoolean(prefs::kProfileProxyHasSavedWebRTCIPHandlingPolicy)) {
    return;
  }
  prefs.SetString(
      kWebRTCIPHandlingPolicyPref,
      prefs.GetString(prefs::kProfileProxySavedWebRTCIPHandlingPolicy));
  prefs.ClearPref(prefs::kProfileProxySavedWebRTCIPHandlingPolicy);
  prefs.ClearPref(prefs::kProfileProxyHasSavedWebRTCIPHandlingPolicy);
}

std::optional<std::string> AcceptLanguagesForCountryCode(
    std::string_view country_code) {
  const std::string normalized = base::ToUpperASCII(country_code);
  if (normalized == "AU") {
    return "en-AU,en";
  }
  if (normalized == "CA") {
    return "en-CA,en";
  }
  if (normalized == "DE") {
    return "de-DE,de";
  }
  if (normalized == "FR") {
    return "fr-FR,fr";
  }
  if (normalized == "GB") {
    return "en-GB,en";
  }
  if (normalized == "JP") {
    return "ja-JP,ja";
  }
  if (normalized == "US") {
    return "en-US,en";
  }
  return std::nullopt;
}

std::optional<net::IPAddress> ParseIpLiteralHost(std::string_view host) {
  if (host.size() > 2 && host.front() == '[' && host.back() == ']') {
    host.remove_prefix(1);
    host.remove_suffix(1);
  }
  return net::IPAddress::FromIPLiteral(host);
}

ProfileProxyGeo MakeProfileProxyGeo(std::string_view country_code,
                                    std::string_view timezone,
                                    double latitude,
                                    double longitude,
                                    std::string_view accept_languages) {
  ProfileProxyGeo geo;
  geo.country_code = std::string(country_code);
  geo.timezone = std::string(timezone);
  geo.latitude = latitude;
  geo.longitude = longitude;
  geo.accept_languages = std::string(accept_languages);
  return geo;
}

std::optional<ProfileProxyGeo> ResolveProfileProxyGeoFromHost(
    std::string_view host) {
  const std::optional<net::IPAddress> ip = ParseIpLiteralHost(host);
  if (!ip) {
    return std::nullopt;
  }
  const auto geo = OfflineGeoIpDatabase::GetInstance()->Lookup(ip->ToString());
  if (!geo) {
    return std::nullopt;
  }
  return MakeProfileProxyGeo(geo->country_code, geo->timezone, geo->latitude,
                             geo->longitude, geo->accept_languages);
}

std::optional<ProfileProxyGeo> GetManualProfileProxyGeo(
    const PrefService& prefs) {
  if (!prefs.GetBoolean(prefs::kProfileProxyManualGeoEnabled)) {
    return std::nullopt;
  }

  ProfileProxyGeo geo;
  geo.country_code = base::ToUpperASCII(
      prefs.GetString(prefs::kProfileProxyManualGeoCountryCode));
  geo.timezone = prefs.GetString(prefs::kProfileProxyManualGeoTimezone);
  geo.latitude = prefs.GetDouble(prefs::kProfileProxyManualGeoLatitude);
  geo.longitude = prefs.GetDouble(prefs::kProfileProxyManualGeoLongitude);
  const auto accept_languages = AcceptLanguagesForCountryCode(geo.country_code);
  if (!accept_languages || geo.country_code.empty() || geo.timezone.empty()) {
    return std::nullopt;
  }
  geo.accept_languages = *accept_languages;
  return geo;
}

void ApplyProfileProxyGeo(PrefService& prefs, const ProfileProxyGeo& geo) {
  prefs.SetString(prefs::kProfileProxyDerivedGeoCountryCode, geo.country_code);
  prefs.SetString(prefs::kProfileProxyDerivedGeoTimezone, geo.timezone);
  prefs.SetDouble(prefs::kProfileProxyDerivedGeoLatitude, geo.latitude);
  prefs.SetDouble(prefs::kProfileProxyDerivedGeoLongitude, geo.longitude);
  prefs.SetBoolean(prefs::kProfileProxyGeoLookupFailed, false);
}

void ClearProfileProxyDerivedGeo(PrefService& prefs) {
  prefs.ClearPref(prefs::kProfileProxyDerivedGeoCountryCode);
  prefs.ClearPref(prefs::kProfileProxyDerivedGeoTimezone);
  prefs.ClearPref(prefs::kProfileProxyDerivedGeoLatitude);
  prefs.ClearPref(prefs::kProfileProxyDerivedGeoLongitude);
}

void ApplyProfileProxyAcceptLanguages(PrefService& prefs,
                                      std::string_view accept_languages) {
  prefs.SetString(prefs::kProfileProxyDerivedAcceptLanguages, accept_languages);
  if (!prefs.FindPreference(kAcceptLanguagesPref)) {
    return;
  }
  if (!prefs.GetBoolean(prefs::kProfileProxyHasSavedAcceptLanguages)) {
    prefs.SetString(prefs::kProfileProxySavedAcceptLanguages,
                    prefs.GetString(kAcceptLanguagesPref));
    prefs.SetBoolean(prefs::kProfileProxyHasSavedAcceptLanguages, true);
  }
  prefs.SetString(kAcceptLanguagesPref, accept_languages);
}

void RestoreProfileProxyAcceptLanguages(PrefService& prefs) {
  if (prefs.FindPreference(kAcceptLanguagesPref) &&
      prefs.GetBoolean(prefs::kProfileProxyHasSavedAcceptLanguages)) {
    prefs.SetString(kAcceptLanguagesPref,
                    prefs.GetString(prefs::kProfileProxySavedAcceptLanguages));
  }
  prefs.ClearPref(prefs::kProfileProxySavedAcceptLanguages);
  prefs.ClearPref(prefs::kProfileProxyHasSavedAcceptLanguages);
  prefs.ClearPref(prefs::kProfileProxyDerivedAcceptLanguages);
}

ProfileProxyConfigConflict GetConflictForPref(const PrefService& prefs,
                                              std::string_view pref_name) {
  const auto* pref = prefs.FindPreference(pref_name);
  if (!pref) {
    return ProfileProxyConfigConflict::kNone;
  }
  if (pref->IsManaged()) {
    return ProfileProxyConfigConflict::kPolicy;
  }
  if (pref->IsExtensionControlled()) {
    return ProfileProxyConfigConflict::kExtension;
  }
  return ProfileProxyConfigConflict::kNone;
}

}  // namespace

bool IsProfileProxyEnabled(const PrefService& prefs) {
  return prefs.GetBoolean(prefs::kProfileProxyEnabled);
}

std::optional<net::ProxyServer> GetProfileProxyServerFromPrefs(
    const PrefService& prefs) {
  const std::optional<net::ProxyServer::Scheme> scheme =
      SchemeFromPref(prefs.GetString(prefs::kProfileProxyScheme));
  if (!scheme) {
    return std::nullopt;
  }

  const std::string& host = prefs.GetString(prefs::kProfileProxyHost);
  const int port = prefs.GetInteger(prefs::kProfileProxyPort);
  if (host.empty() || port <= 0 || port > 65535) {
    return std::nullopt;
  }

  std::string proxy_host(host);
  const auto ip = ParseIpLiteralHost(host);
  if (ip && ip->IsIPv6()) {
    proxy_host = "[" + ip->ToString() + "]";
  }
  const net::ProxyServer parsed_server =
      net::ProxyServer::FromSchemeHostAndPort(*scheme, proxy_host,
                                              static_cast<uint16_t>(port));
  if (!parsed_server.is_valid()) {
    return std::nullopt;
  }

  return net::ProxyServer(
      *scheme, net::HostPortPair(prefs.GetString(prefs::kProfileProxyUsername),
                                 prefs.GetString(prefs::kProfileProxyPassword),
                                 parsed_server.host_port_pair().host(),
                                 parsed_server.host_port_pair().port()));
}

ProfileProxyConfigConflict GetProfileProxyConfigConflict(
    const PrefService& prefs) {
  auto conflict = GetConflictForPref(prefs, proxy_config::prefs::kProxy);
  if (conflict != ProfileProxyConfigConflict::kNone) {
    return conflict;
  }
  return GetConflictForPref(prefs, proxy_config::prefs::kProxyOverrideRules);
}

bool ShouldUseProfileProxy(const PrefService& prefs) {
  return IsProfileProxyEnabled(prefs) &&
         GetProfileProxyConfigConflict(prefs) ==
             ProfileProxyConfigConflict::kNone &&
         GetProfileProxyServerFromPrefs(prefs).has_value();
}

std::string_view ProfileProxyConfigConflictWarning(
    ProfileProxyConfigConflict conflict) {
  switch (conflict) {
    case ProfileProxyConfigConflict::kNone:
      return std::string_view();
    case ProfileProxyConfigConflict::kPolicy:
      return "Enterprise policy controls this profile's proxy settings.";
    case ProfileProxyConfigConflict::kExtension:
      return "An extension controls this profile's proxy settings.";
  }
}

void SyncProfileProxyWebRTCPolicy(PrefService& prefs) {
  if (ShouldUseProfileProxy(prefs)) {
    ApplyProfileProxyWebRTCPolicy(prefs);
    return;
  }
  RestoreProfileProxyWebRTCPolicy(prefs);
}

void SyncProfileProxyLanguage(PrefService& prefs) {
  if (!ShouldUseProfileProxy(prefs)) {
    RestoreProfileProxyAcceptLanguages(prefs);
    ClearProfileProxyDerivedGeo(prefs);
    prefs.SetBoolean(prefs::kProfileProxyGeoLookupFailed, false);
    return;
  }

  auto geo =
      ResolveProfileProxyGeoFromHost(prefs.GetString(prefs::kProfileProxyHost));
  if (!geo) {
    geo = GetManualProfileProxyGeo(prefs);
  }
  if (!geo) {
    ClearProfileProxyDerivedGeo(prefs);
    prefs.SetBoolean(prefs::kProfileProxyGeoLookupFailed, true);
    RestoreProfileProxyAcceptLanguages(prefs);
    return;
  }
  ApplyProfileProxyGeo(prefs, *geo);
  ApplyProfileProxyAcceptLanguages(prefs, geo->accept_languages);
}

void SyncProfileProxyDerivedPrefs(PrefService& prefs) {
  SyncProfileProxyWebRTCPolicy(prefs);
  SyncProfileProxyLanguage(prefs);
}

std::optional<std::string> GetProfileProxyAcceptLanguagesForPrefs(
    const PrefService& prefs) {
  if (!ShouldUseProfileProxy(prefs)) {
    return std::nullopt;
  }
  const std::string& accept_languages =
      prefs.GetString(prefs::kProfileProxyDerivedAcceptLanguages);
  if (accept_languages.empty()) {
    return std::nullopt;
  }
  return accept_languages;
}

std::optional<ProfileProxyGeo> GetProfileProxyGeoForPrefs(
    const PrefService& prefs) {
  if (!ShouldUseProfileProxy(prefs)) {
    return std::nullopt;
  }

  const std::string& country_code =
      prefs.GetString(prefs::kProfileProxyDerivedGeoCountryCode);
  const std::string& timezone =
      prefs.GetString(prefs::kProfileProxyDerivedGeoTimezone);
  if (country_code.empty() || timezone.empty()) {
    return std::nullopt;
  }

  ProfileProxyGeo geo;
  geo.country_code = country_code;
  geo.timezone = timezone;
  geo.latitude = prefs.GetDouble(prefs::kProfileProxyDerivedGeoLatitude);
  geo.longitude = prefs.GetDouble(prefs::kProfileProxyDerivedGeoLongitude);
  const auto accept_languages = GetProfileProxyAcceptLanguagesForPrefs(prefs);
  geo.accept_languages = accept_languages.value_or(std::string());
  return geo;
}

std::vector<std::string> GetProfileProxyLanguagesForPrefs(
    const PrefService& prefs,
    const std::vector<std::string>& fallback_languages) {
  const auto accept_languages = GetProfileProxyAcceptLanguagesForPrefs(prefs);
  if (!accept_languages) {
    return fallback_languages;
  }
  std::vector<std::string> languages = base::SplitString(
      *accept_languages, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  if (languages.empty()) {
    return fallback_languages;
  }
  return languages;
}

std::string_view ProfileProxyGeoWarning(const PrefService& prefs) {
  if (!ShouldUseProfileProxy(prefs) ||
      !prefs.GetBoolean(prefs::kProfileProxyGeoLookupFailed)) {
    return std::string_view();
  }
  return kGeoLookupWarning;
}

void SetProfileProxyLastError(PrefService& prefs, int net_error) {
  prefs.SetInteger(prefs::kProfileProxyLastErrorCode, net_error);

  switch (net_error) {
    case net::ERR_INVALID_AUTH_CREDENTIALS:
      prefs.SetString(prefs::kProfileProxyLastError,
                      "Proxy authentication failed. Check username/password.");
      return;
    case net::ERR_SOCKS_CONNECTION_FAILED:
      prefs.SetString(prefs::kProfileProxyLastError,
                      "SOCKS5 proxy authentication failed or was rejected.");
      return;
    case net::ERR_PROXY_AUTH_UNSUPPORTED:
      prefs.SetString(prefs::kProfileProxyLastError,
                      "Proxy authentication method is unsupported.");
      return;
    case net::ERR_PROXY_CONNECTION_FAILED:
    case net::ERR_TUNNEL_CONNECTION_FAILED:
      prefs.SetString(prefs::kProfileProxyLastError,
                      "Proxy connection failed. Check host and port.");
      return;
    default:
      prefs.SetString(prefs::kProfileProxyLastError,
                      "Proxy connection failed.");
      return;
  }
}

void ClearProfileProxyLastError(PrefService& prefs) {
  prefs.ClearPref(prefs::kProfileProxyLastError);
  prefs.ClearPref(prefs::kProfileProxyLastErrorCode);
}

}  // namespace fingerprint_browser
