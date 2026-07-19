/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_FINGERPRINT_BROWSER_BROWSER_PREF_NAMES_H_
#define BRAVE_COMPONENTS_FINGERPRINT_BROWSER_BROWSER_PREF_NAMES_H_

class PrefRegistrySimple;

namespace fingerprint_browser::prefs {

inline constexpr char kPersona[] = "brave.fingerprint_browser.persona";
inline constexpr char kProfileProxyEnabled[] =
    "brave.fingerprint_browser.proxy.enabled";
inline constexpr char kProfileProxyScheme[] =
    "brave.fingerprint_browser.proxy.scheme";
inline constexpr char kProfileProxyHost[] =
    "brave.fingerprint_browser.proxy.host";
inline constexpr char kProfileProxyPort[] =
    "brave.fingerprint_browser.proxy.port";
inline constexpr char kProfileProxyUsername[] =
    "brave.fingerprint_browser.proxy.username";
inline constexpr char kProfileProxyPassword[] =
    "brave.fingerprint_browser.proxy.password";
inline constexpr char kProfileProxyEncryptedPassword[] =
    "brave.fingerprint_browser.proxy.encrypted_password";
inline constexpr char kProfileProxyCredentialGeneration[] =
    "brave.fingerprint_browser.proxy.credential_generation";
inline constexpr char kProfileProxyState[] =
    "brave.fingerprint_browser.proxy.state";
inline constexpr char kProfileProxyStatusMessage[] =
    "brave.fingerprint_browser.proxy.status_message";
inline constexpr char kProfileProxyChangeWarning[] =
    "brave.fingerprint_browser.proxy.change_warning";
inline constexpr char kProfileProxyEgressIp[] =
    "brave.fingerprint_browser.proxy.egress_ip";
inline constexpr char kProfileProxyCountryName[] =
    "brave.fingerprint_browser.proxy.country_name";
inline constexpr char kProfileProxyRegionName[] =
    "brave.fingerprint_browser.proxy.region_name";
inline constexpr char kProfileProxyCityName[] =
    "brave.fingerprint_browser.proxy.city_name";
inline constexpr char kProfileProxyGeoProvider[] =
    "brave.fingerprint_browser.proxy.geo.provider";
inline constexpr char kProfileProxyLastVerifiedTime[] =
    "brave.fingerprint_browser.proxy.last_verified_time";
inline constexpr char kProfileProxyLastError[] =
    "brave.fingerprint_browser.proxy.last_error";
inline constexpr char kProfileProxyLastErrorCode[] =
    "brave.fingerprint_browser.proxy.last_error_code";
inline constexpr char kProfileProxyHasSavedWebRTCIPHandlingPolicy[] =
    "brave.fingerprint_browser.proxy.webrtc.has_saved_ip_handling_policy";
inline constexpr char kProfileProxySavedWebRTCIPHandlingPolicy[] =
    "brave.fingerprint_browser.proxy.webrtc.saved_ip_handling_policy";
inline constexpr char kProfileProxyHasSavedAcceptLanguages[] =
    "brave.fingerprint_browser.proxy.language.has_saved_accept_languages";
inline constexpr char kProfileProxySavedAcceptLanguages[] =
    "brave.fingerprint_browser.proxy.language.saved_accept_languages";
inline constexpr char kProfileProxyDerivedAcceptLanguages[] =
    "brave.fingerprint_browser.proxy.language.derived_accept_languages";
inline constexpr char kProfileProxyManualGeoEnabled[] =
    "brave.fingerprint_browser.proxy.geo.manual_enabled";
inline constexpr char kProfileProxyManualGeoCountryCode[] =
    "brave.fingerprint_browser.proxy.geo.manual_country_code";
inline constexpr char kProfileProxyManualGeoTimezone[] =
    "brave.fingerprint_browser.proxy.geo.manual_timezone";
inline constexpr char kProfileProxyManualGeoLatitude[] =
    "brave.fingerprint_browser.proxy.geo.manual_latitude";
inline constexpr char kProfileProxyManualGeoLongitude[] =
    "brave.fingerprint_browser.proxy.geo.manual_longitude";
inline constexpr char kProfileProxyDerivedGeoCountryCode[] =
    "brave.fingerprint_browser.proxy.geo.derived_country_code";
inline constexpr char kProfileProxyDerivedGeoTimezone[] =
    "brave.fingerprint_browser.proxy.geo.derived_timezone";
inline constexpr char kProfileProxyDerivedGeoLatitude[] =
    "brave.fingerprint_browser.proxy.geo.derived_latitude";
inline constexpr char kProfileProxyDerivedGeoLongitude[] =
    "brave.fingerprint_browser.proxy.geo.derived_longitude";
inline constexpr char kProfileProxyGeoLookupFailed[] =
    "brave.fingerprint_browser.proxy.geo.lookup_failed";

inline constexpr char kProfileProxySchemeHttp[] = "http";
inline constexpr char kProfileProxySchemeHttps[] = "https";
inline constexpr char kProfileProxySchemeSocks5[] = "socks5";

void RegisterProfilePrefs(PrefRegistrySimple* registry);

}  // namespace fingerprint_browser::prefs

#endif  // BRAVE_COMPONENTS_FINGERPRINT_BROWSER_BROWSER_PREF_NAMES_H_
