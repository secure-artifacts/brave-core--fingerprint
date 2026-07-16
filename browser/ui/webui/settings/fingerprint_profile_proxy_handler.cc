/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/webui/settings/fingerprint_profile_proxy_handler.h"

#include <memory>
#include <string>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "brave/components/fingerprint_browser/browser/pref_names.h"
#include "brave/components/fingerprint_browser/browser/profile_proxy_config.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "third_party/icu/source/i18n/unicode/timezone.h"
#include "third_party/icu/source/common/unicode/unistr.h"

namespace {

constexpr char kEnabledKey[] = "enabled";
constexpr char kSchemeKey[] = "scheme";
constexpr char kHostKey[] = "host";
constexpr char kPortKey[] = "port";
constexpr char kUsernameKey[] = "username";
constexpr char kPasswordKey[] = "password";
constexpr char kConflictWarningKey[] = "conflictWarning";
constexpr char kManualCountryCodeKey[] = "manualCountryCode";
constexpr char kManualTimezoneKey[] = "manualTimezone";
constexpr char kManualLatitudeKey[] = "manualLatitude";
constexpr char kManualLongitudeKey[] = "manualLongitude";
constexpr char kGeoWarningKey[] = "geoWarning";
constexpr char kDerivedCountryCodeKey[] = "derivedCountryCode";
constexpr char kDerivedTimezoneKey[] = "derivedTimezone";
constexpr char kDerivedLatitudeKey[] = "derivedLatitude";
constexpr char kDerivedLongitudeKey[] = "derivedLongitude";
constexpr char kSuccessKey[] = "success";
constexpr char kErrorKey[] = "error";
constexpr char kMessageKey[] = "message";
constexpr char kCodeKey[] = "code";

std::string TrimString(const std::string& value) {
  std::string trimmed;
  base::TrimWhitespaceASCII(value, base::TRIM_ALL, &trimmed);
  return trimmed;
}

bool IsCountryCode(std::string_view value) {
  return value.size() == 2 && base::IsAsciiAlpha(value[0]) &&
         base::IsAsciiAlpha(value[1]);
}

bool IsIanaTimezone(std::string_view value) {
  std::unique_ptr<icu::TimeZone> timezone(
      icu::TimeZone::createTimeZone(icu::UnicodeString::fromUTF8(value)));
  return timezone && *timezone != icu::TimeZone::getUnknown();
}

std::string ManualGeoValidationError(const base::DictValue& config) {
  const std::string country =
      TrimString(config.FindString(kManualCountryCodeKey)
                     ? *config.FindString(kManualCountryCodeKey)
                     : std::string());
  const std::string timezone =
      TrimString(config.FindString(kManualTimezoneKey)
                     ? *config.FindString(kManualTimezoneKey)
                     : std::string());
  const std::string latitude =
      TrimString(config.FindString(kManualLatitudeKey)
                     ? *config.FindString(kManualLatitudeKey)
                     : std::string());
  const std::string longitude =
      TrimString(config.FindString(kManualLongitudeKey)
                     ? *config.FindString(kManualLongitudeKey)
                     : std::string());
  const bool has_value = !country.empty() || !timezone.empty() ||
                         !latitude.empty() || !longitude.empty();
  if (!has_value) {
    return std::string();
  }
  if (country.empty() || timezone.empty() || latitude.empty() ||
      longitude.empty()) {
    return "Manual geo fallback requires country, time zone, latitude, and "
           "longitude.";
  }
  if (!IsCountryCode(country)) {
    return "Country code must be two letters.";
  }
  if (!IsIanaTimezone(timezone)) {
    return "Time zone must be a valid IANA identifier.";
  }

  double parsed_latitude = 0.0;
  double parsed_longitude = 0.0;
  if (!base::StringToDouble(latitude, &parsed_latitude) ||
      parsed_latitude < -90.0 || parsed_latitude > 90.0) {
    return "Latitude must be between -90 and 90.";
  }
  if (!base::StringToDouble(longitude, &parsed_longitude) ||
      parsed_longitude < -180.0 || parsed_longitude > 180.0) {
    return "Longitude must be between -180 and 180.";
  }
  return std::string();
}

void SaveManualGeoPrefs(PrefService* prefs, const base::DictValue& config) {
  const std::string country =
      TrimString(config.FindString(kManualCountryCodeKey)
                     ? *config.FindString(kManualCountryCodeKey)
                     : std::string());
  const std::string timezone =
      TrimString(config.FindString(kManualTimezoneKey)
                     ? *config.FindString(kManualTimezoneKey)
                     : std::string());
  const std::string latitude =
      TrimString(config.FindString(kManualLatitudeKey)
                     ? *config.FindString(kManualLatitudeKey)
                     : std::string());
  const std::string longitude =
      TrimString(config.FindString(kManualLongitudeKey)
                     ? *config.FindString(kManualLongitudeKey)
                     : std::string());
  if (country.empty() && timezone.empty() && latitude.empty() &&
      longitude.empty()) {
    prefs->ClearPref(fingerprint_browser::prefs::kProfileProxyManualGeoEnabled);
    prefs->ClearPref(
        fingerprint_browser::prefs::kProfileProxyManualGeoCountryCode);
    prefs->ClearPref(
        fingerprint_browser::prefs::kProfileProxyManualGeoTimezone);
    prefs->ClearPref(
        fingerprint_browser::prefs::kProfileProxyManualGeoLatitude);
    prefs->ClearPref(
        fingerprint_browser::prefs::kProfileProxyManualGeoLongitude);
    return;
  }

  double parsed_latitude = 0.0;
  double parsed_longitude = 0.0;
  CHECK(base::StringToDouble(latitude, &parsed_latitude));
  CHECK(base::StringToDouble(longitude, &parsed_longitude));
  prefs->SetBoolean(fingerprint_browser::prefs::kProfileProxyManualGeoEnabled,
                    true);
  prefs->SetString(
      fingerprint_browser::prefs::kProfileProxyManualGeoCountryCode,
      base::ToUpperASCII(country));
  prefs->SetString(fingerprint_browser::prefs::kProfileProxyManualGeoTimezone,
                   timezone);
  prefs->SetDouble(fingerprint_browser::prefs::kProfileProxyManualGeoLatitude,
                   parsed_latitude);
  prefs->SetDouble(fingerprint_browser::prefs::kProfileProxyManualGeoLongitude,
                   parsed_longitude);
}

}  // namespace

FingerprintProfileProxyHandler::FingerprintProfileProxyHandler() = default;
FingerprintProfileProxyHandler::~FingerprintProfileProxyHandler() = default;

void FingerprintProfileProxyHandler::RegisterMessages() {
  profile_ = Profile::FromWebUI(web_ui());

  web_ui()->RegisterMessageCallback(
      "fingerprint_profile_proxy.getConfig",
      base::BindRepeating(&FingerprintProfileProxyHandler::GetConfig,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "fingerprint_profile_proxy.setConfig",
      base::BindRepeating(&FingerprintProfileProxyHandler::SetConfig,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "fingerprint_profile_proxy.getLastError",
      base::BindRepeating(&FingerprintProfileProxyHandler::GetLastError,
                          base::Unretained(this)));

  pref_change_registrar_.Init(profile_->GetPrefs());
  pref_change_registrar_.Add(
      fingerprint_browser::prefs::kProfileProxyLastError,
      base::BindRepeating(&FingerprintProfileProxyHandler::OnLastErrorChanged,
                          base::Unretained(this)));
  pref_change_registrar_.Add(
      fingerprint_browser::prefs::kProfileProxyLastErrorCode,
      base::BindRepeating(&FingerprintProfileProxyHandler::OnLastErrorChanged,
                          base::Unretained(this)));
}

void FingerprintProfileProxyHandler::GetConfig(const base::ListValue& args) {
  CHECK_EQ(1u, args.size());

  AllowJavascript();
  ResolveJavascriptCallback(args[0], base::Value(BuildConfig()));
}

void FingerprintProfileProxyHandler::SetConfig(const base::ListValue& args) {
  CHECK_EQ(2u, args.size());
  CHECK(args[1].is_dict());

  AllowJavascript();

  const base::DictValue& config = args[1].GetDict();
  const std::string validation_error = ValidateConfig(config);
  if (!validation_error.empty()) {
    base::DictValue result;
    result.Set(kSuccessKey, false);
    result.Set(kErrorKey, validation_error);
    ResolveJavascriptCallback(args[0], base::Value(std::move(result)));
    return;
  }

  PrefService* prefs = profile_->GetPrefs();
  const std::string* scheme = config.FindString(kSchemeKey);
  const std::string* host = config.FindString(kHostKey);
  const std::string* username = config.FindString(kUsernameKey);
  const std::string* password = config.FindString(kPasswordKey);

  prefs->SetBoolean(fingerprint_browser::prefs::kProfileProxyEnabled,
                    config.FindBool(kEnabledKey).value_or(false));
  prefs->SetString(fingerprint_browser::prefs::kProfileProxyScheme, *scheme);
  prefs->SetString(fingerprint_browser::prefs::kProfileProxyHost,
                   TrimString(host ? *host : std::string()));
  prefs->SetInteger(fingerprint_browser::prefs::kProfileProxyPort,
                    config.FindInt(kPortKey).value_or(0));
  prefs->SetString(fingerprint_browser::prefs::kProfileProxyUsername,
                   username ? *username : std::string());
  prefs->SetString(fingerprint_browser::prefs::kProfileProxyPassword,
                   password ? *password : std::string());
  SaveManualGeoPrefs(prefs, config);
  fingerprint_browser::ClearProfileProxyLastError(*prefs);
  fingerprint_browser::SyncProfileProxyDerivedPrefs(*prefs);
  content::WebContents::SyncRendererPrefsForBrowserContext(profile_);

  base::DictValue result;
  result.Set(kSuccessKey, true);
  result.Set(kErrorKey, std::string());
  result.Set(kConflictWarningKey,
             std::string(fingerprint_browser::ProfileProxyConfigConflictWarning(
                 fingerprint_browser::GetProfileProxyConfigConflict(*prefs))));
  result.Set(kGeoWarningKey,
             std::string(fingerprint_browser::ProfileProxyGeoWarning(*prefs)));
  ResolveJavascriptCallback(args[0], base::Value(std::move(result)));
}

void FingerprintProfileProxyHandler::GetLastError(const base::ListValue& args) {
  CHECK_EQ(1u, args.size());

  AllowJavascript();
  ResolveJavascriptCallback(args[0], base::Value(BuildLastError()));
}

void FingerprintProfileProxyHandler::OnLastErrorChanged() {
  if (!IsJavascriptAllowed()) {
    return;
  }

  FireWebUIListener("fingerprint-profile-proxy-error-changed",
                    base::Value(BuildLastError()));
}

base::DictValue FingerprintProfileProxyHandler::BuildConfig() const {
  const PrefService* prefs = profile_->GetPrefs();
  base::DictValue config;
  config.Set(
      kEnabledKey,
      prefs->GetBoolean(fingerprint_browser::prefs::kProfileProxyEnabled));
  config.Set(kSchemeKey,
             prefs->GetString(fingerprint_browser::prefs::kProfileProxyScheme));
  config.Set(kHostKey,
             prefs->GetString(fingerprint_browser::prefs::kProfileProxyHost));
  config.Set(kPortKey,
             prefs->GetInteger(fingerprint_browser::prefs::kProfileProxyPort));
  config.Set(
      kUsernameKey,
      prefs->GetString(fingerprint_browser::prefs::kProfileProxyUsername));
  config.Set(
      kPasswordKey,
      prefs->GetString(fingerprint_browser::prefs::kProfileProxyPassword));
  config.Set(kConflictWarningKey,
             std::string(fingerprint_browser::ProfileProxyConfigConflictWarning(
                 fingerprint_browser::GetProfileProxyConfigConflict(*prefs))));
  if (prefs->GetBoolean(
          fingerprint_browser::prefs::kProfileProxyManualGeoEnabled)) {
    config.Set(
        kManualCountryCodeKey,
        prefs->GetString(
            fingerprint_browser::prefs::kProfileProxyManualGeoCountryCode));
    config.Set(kManualTimezoneKey,
               prefs->GetString(
                   fingerprint_browser::prefs::kProfileProxyManualGeoTimezone));
    config.Set(
        kManualLatitudeKey,
        base::NumberToString(prefs->GetDouble(
            fingerprint_browser::prefs::kProfileProxyManualGeoLatitude)));
    config.Set(
        kManualLongitudeKey,
        base::NumberToString(prefs->GetDouble(
            fingerprint_browser::prefs::kProfileProxyManualGeoLongitude)));
  } else {
    config.Set(kManualCountryCodeKey, std::string());
    config.Set(kManualTimezoneKey, std::string());
    config.Set(kManualLatitudeKey, std::string());
    config.Set(kManualLongitudeKey, std::string());
  }
  config.Set(kGeoWarningKey,
             std::string(fingerprint_browser::ProfileProxyGeoWarning(*prefs)));
  const auto geo = fingerprint_browser::GetProfileProxyGeoForPrefs(*prefs);
  config.Set(kDerivedCountryCodeKey, geo ? geo->country_code : std::string());
  config.Set(kDerivedTimezoneKey, geo ? geo->timezone : std::string());
  config.Set(kDerivedLatitudeKey,
             geo ? base::NumberToString(geo->latitude) : std::string());
  config.Set(kDerivedLongitudeKey,
             geo ? base::NumberToString(geo->longitude) : std::string());
  return config;
}

base::DictValue FingerprintProfileProxyHandler::BuildLastError() const {
  const PrefService* prefs = profile_->GetPrefs();
  base::DictValue error;
  error.Set(
      kMessageKey,
      prefs->GetString(fingerprint_browser::prefs::kProfileProxyLastError));
  error.Set(kCodeKey,
            prefs->GetInteger(
                fingerprint_browser::prefs::kProfileProxyLastErrorCode));
  return error;
}

std::string FingerprintProfileProxyHandler::ValidateConfig(
    const base::DictValue& config) const {
  const std::string* scheme = config.FindString(kSchemeKey);
  if (!scheme ||
      (*scheme != fingerprint_browser::prefs::kProfileProxySchemeHttp &&
       *scheme != fingerprint_browser::prefs::kProfileProxySchemeSocks5)) {
    return "Select HTTP or SOCKS5.";
  }

  const bool enabled = config.FindBool(kEnabledKey).value_or(false);
  const std::string* host = config.FindString(kHostKey);
  const std::string trimmed_host = TrimString(host ? *host : std::string());
  const int port = config.FindInt(kPortKey).value_or(0);
  const bool has_proxy_value = !trimmed_host.empty() || port != 0;

  if ((enabled || has_proxy_value) && trimmed_host.empty()) {
    return "Host is required when proxy is enabled.";
  }

  if ((enabled || has_proxy_value) && (port <= 0 || port > 65535)) {
    return "Port must be between 1 and 65535.";
  }

  return ManualGeoValidationError(config);
}
