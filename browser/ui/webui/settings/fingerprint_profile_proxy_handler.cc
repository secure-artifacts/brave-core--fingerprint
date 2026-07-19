/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/webui/settings/fingerprint_profile_proxy_handler.h"

#include <string>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "brave/browser/fingerprint_browser/fingerprint_proxy_service_factory.h"
#include "brave/components/fingerprint_browser/browser/pref_names.h"
#include "chrome/browser/profiles/profile.h"

namespace {

constexpr char kStateKey[] = "state";
constexpr char kStatusMessageKey[] = "statusMessage";
constexpr char kChangeWarningKey[] = "changeWarning";
constexpr char kEnabledKey[] = "enabled";
constexpr char kSchemeKey[] = "scheme";
constexpr char kHostKey[] = "host";
constexpr char kPortKey[] = "port";
constexpr char kUsernameKey[] = "username";
constexpr char kPasswordKey[] = "password";
constexpr char kHasSavedPasswordKey[] = "hasSavedPassword";
constexpr char kEgressIpKey[] = "egressIp";
constexpr char kGeoProviderKey[] = "geoProvider";
constexpr char kLastVerifiedKey[] = "lastVerified";
constexpr char kGeoKey[] = "geo";
constexpr char kCountryCodeKey[] = "countryCode";
constexpr char kCountryNameKey[] = "countryName";
constexpr char kRegionNameKey[] = "regionName";
constexpr char kCityNameKey[] = "cityName";
constexpr char kTimezoneKey[] = "timezone";
constexpr char kLatitudeKey[] = "latitude";
constexpr char kLongitudeKey[] = "longitude";
constexpr char kAcceptLanguagesKey[] = "acceptLanguages";
constexpr char kSuccessKey[] = "success";
constexpr char kVerificationIdKey[] = "verificationId";
constexpr char kErrorKey[] = "error";

std::string TrimString(std::string value) {
  base::TrimWhitespaceASCII(value, base::TRIM_ALL, &value);
  return value;
}

}  // namespace

FingerprintProfileProxyHandler::FingerprintProfileProxyHandler() = default;

FingerprintProfileProxyHandler::~FingerprintProfileProxyHandler() {
  if (observing_service_) {
    service_->RemoveObserver(this);
  }
}

void FingerprintProfileProxyHandler::RegisterMessages() {
  profile_ = Profile::FromWebUI(web_ui());
  service_ = fingerprint_browser::FingerprintProxyServiceFactory::GetForProfile(
      profile_);

  web_ui()->RegisterMessageCallback(
      "fingerprint_profile_proxy.getState",
      base::BindRepeating(&FingerprintProfileProxyHandler::GetState,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "fingerprint_profile_proxy.verifyDraft",
      base::BindRepeating(&FingerprintProfileProxyHandler::VerifyDraft,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "fingerprint_profile_proxy.applyVerified",
      base::BindRepeating(&FingerprintProfileProxyHandler::ApplyVerified,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "fingerprint_profile_proxy.revalidate",
      base::BindRepeating(&FingerprintProfileProxyHandler::Revalidate,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "fingerprint_profile_proxy.disable",
      base::BindRepeating(&FingerprintProfileProxyHandler::Disable,
                          base::Unretained(this)));
}

void FingerprintProfileProxyHandler::OnJavascriptAllowed() {
  if (service_ && !observing_service_) {
    service_->AddObserver(this);
    observing_service_ = true;
  }
}

void FingerprintProfileProxyHandler::OnJavascriptDisallowed() {
  if (service_ && observing_service_) {
    service_->RemoveObserver(this);
    observing_service_ = false;
  }
}

void FingerprintProfileProxyHandler::GetState(const base::ListValue& args) {
  CHECK_EQ(1u, args.size());
  AllowJavascript();
  ResolveJavascriptCallback(args[0], base::Value(BuildState()));
}

void FingerprintProfileProxyHandler::VerifyDraft(const base::ListValue& args) {
  CHECK_EQ(2u, args.size());
  CHECK(args[1].is_dict());
  AllowJavascript();

  if (!service_) {
    fingerprint_browser::ProxyVerificationResult result;
    result.error = "Profile proxy is unavailable in this window.";
    OnVerificationComplete(args[0].Clone(), std::move(result));
    return;
  }

  const base::DictValue& value = args[1].GetDict();
  fingerprint_browser::ProfileProxyDraft draft;
  draft.scheme = value.FindString(kSchemeKey) ? *value.FindString(kSchemeKey)
                                              : std::string();
  draft.host = TrimString(
      value.FindString(kHostKey) ? *value.FindString(kHostKey) : std::string());
  draft.port = value.FindInt(kPortKey).value_or(0);
  draft.username = value.FindString(kUsernameKey)
                       ? *value.FindString(kUsernameKey)
                       : std::string();
  draft.password = value.FindString(kPasswordKey)
                       ? *value.FindString(kPasswordKey)
                       : std::string();

  service_->VerifyDraft(
      std::move(draft),
      base::BindOnce(&FingerprintProfileProxyHandler::OnVerificationComplete,
                     weak_factory_.GetWeakPtr(), args[0].Clone()));
}

void FingerprintProfileProxyHandler::ApplyVerified(
    const base::ListValue& args) {
  CHECK_EQ(2u, args.size());
  CHECK(args[1].is_string());
  AllowJavascript();

  if (!service_) {
    fingerprint_browser::ProxyApplyResult result;
    result.error = "Profile proxy is unavailable in this window.";
    OnApplyComplete(args[0].Clone(), std::move(result));
    return;
  }
  service_->ApplyVerified(
      args[1].GetString(),
      base::BindOnce(&FingerprintProfileProxyHandler::OnApplyComplete,
                     weak_factory_.GetWeakPtr(), args[0].Clone()));
}

void FingerprintProfileProxyHandler::Revalidate(const base::ListValue& args) {
  CHECK_EQ(1u, args.size());
  AllowJavascript();

  if (!service_) {
    fingerprint_browser::ProxyVerificationResult result;
    result.error = "Profile proxy is unavailable in this window.";
    OnVerificationComplete(args[0].Clone(), std::move(result));
    return;
  }
  service_->Revalidate(
      base::BindOnce(&FingerprintProfileProxyHandler::OnVerificationComplete,
                     weak_factory_.GetWeakPtr(), args[0].Clone()));
}

void FingerprintProfileProxyHandler::Disable(const base::ListValue& args) {
  CHECK_EQ(1u, args.size());
  AllowJavascript();

  if (!service_) {
    OnDisableComplete(args[0].Clone());
    return;
  }
  service_->Disable(
      base::BindOnce(&FingerprintProfileProxyHandler::OnDisableComplete,
                     weak_factory_.GetWeakPtr(), args[0].Clone()));
}

void FingerprintProfileProxyHandler::OnVerificationComplete(
    base::Value callback_id,
    fingerprint_browser::ProxyVerificationResult result) {
  ResolveJavascriptCallback(callback_id,
                            base::Value(BuildVerificationResult(result)));
}

void FingerprintProfileProxyHandler::OnApplyComplete(
    base::Value callback_id,
    fingerprint_browser::ProxyApplyResult result) {
  base::DictValue value;
  value.Set(kSuccessKey, result.success);
  value.Set(kErrorKey, result.error);
  ResolveJavascriptCallback(callback_id, base::Value(std::move(value)));
}

void FingerprintProfileProxyHandler::OnDisableComplete(
    base::Value callback_id) {
  base::DictValue value;
  value.Set(kSuccessKey, true);
  value.Set(kErrorKey, std::string());
  ResolveJavascriptCallback(callback_id, base::Value(std::move(value)));
}

void FingerprintProfileProxyHandler::OnFingerprintProxyStateChanged() {
  if (IsJavascriptAllowed()) {
    FireWebUIListener("fingerprint-profile-proxy-state-changed",
                      base::Value(BuildState()));
  }
}

base::DictValue FingerprintProfileProxyHandler::BuildState() const {
  base::DictValue value;
  if (!service_) {
    value.Set(kStateKey, fingerprint_browser::kProxyStateConflict);
    value.Set(kStatusMessageKey,
              "Profile proxy is unavailable in this window.");
    value.Set(kEnabledKey, false);
    value.Set(kHasSavedPasswordKey, false);
    return value;
  }

  const fingerprint_browser::FingerprintProxyState state = service_->GetState();
  value.Set(kStateKey, state.state);
  value.Set(kStatusMessageKey, state.status_message);
  value.Set(kChangeWarningKey, state.change_warning);
  value.Set(kEnabledKey, state.enabled);
  value.Set(kSchemeKey, state.scheme);
  value.Set(kHostKey, state.host);
  value.Set(kPortKey, state.port);
  value.Set(kUsernameKey, state.username);
  value.Set(kHasSavedPasswordKey, state.has_saved_password);
  value.Set(kEgressIpKey, state.egress_ip);
  value.Set(kGeoProviderKey, state.geo_provider);
  value.Set(kLastVerifiedKey,
            state.last_verified.is_null()
                ? 0.0
                : state.last_verified.InMillisecondsFSinceUnixEpoch());
  if (state.geo) {
    value.Set(kGeoKey, BuildGeo(*state.geo));
  }
  return value;
}

base::DictValue FingerprintProfileProxyHandler::BuildVerificationResult(
    const fingerprint_browser::ProxyVerificationResult& result) const {
  base::DictValue value;
  value.Set(kSuccessKey, result.success);
  value.Set(kVerificationIdKey, result.verification_id);
  value.Set(kErrorKey, result.error);
  value.Set(kEgressIpKey, result.egress_ip);
  value.Set(kGeoProviderKey, result.geo_provider);
  if (result.geo) {
    value.Set(kGeoKey, BuildGeo(*result.geo));
  }
  return value;
}

base::DictValue FingerprintProfileProxyHandler::BuildGeo(
    const fingerprint_browser::ProfileProxyGeo& geo) const {
  base::DictValue value;
  value.Set(kCountryCodeKey, geo.country_code);
  value.Set(kCountryNameKey, geo.country_name);
  value.Set(kRegionNameKey, geo.region_name);
  value.Set(kCityNameKey, geo.city_name);
  value.Set(kTimezoneKey, geo.timezone);
  value.Set(kLatitudeKey, geo.latitude);
  value.Set(kLongitudeKey, geo.longitude);
  value.Set(kAcceptLanguagesKey, geo.accept_languages);
  return value;
}
