/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/fingerprint_browser/browser/pref_names.h"

#include <string>

#include "components/prefs/pref_registry_simple.h"

namespace fingerprint_browser::prefs {

void RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterDictionaryPref(kPersona);
  registry->RegisterBooleanPref(kProfileProxyEnabled, false);
  registry->RegisterStringPref(kProfileProxyScheme, kProfileProxySchemeHttp);
  registry->RegisterStringPref(kProfileProxyHost, std::string());
  registry->RegisterIntegerPref(kProfileProxyPort, 0);
  registry->RegisterStringPref(kProfileProxyUsername, std::string());
  registry->RegisterStringPref(kProfileProxyPassword, std::string());
  registry->RegisterStringPref(kProfileProxyLastError, std::string());
  registry->RegisterIntegerPref(kProfileProxyLastErrorCode, 0);
  registry->RegisterBooleanPref(kProfileProxyHasSavedWebRTCIPHandlingPolicy,
                                false);
  registry->RegisterStringPref(kProfileProxySavedWebRTCIPHandlingPolicy,
                               std::string());
  registry->RegisterBooleanPref(kProfileProxyHasSavedAcceptLanguages, false);
  registry->RegisterStringPref(kProfileProxySavedAcceptLanguages,
                               std::string());
  registry->RegisterStringPref(kProfileProxyDerivedAcceptLanguages,
                               std::string());
  registry->RegisterBooleanPref(kProfileProxyManualGeoEnabled, false);
  registry->RegisterStringPref(kProfileProxyManualGeoCountryCode,
                               std::string());
  registry->RegisterStringPref(kProfileProxyManualGeoTimezone, std::string());
  registry->RegisterDoublePref(kProfileProxyManualGeoLatitude, 0.0);
  registry->RegisterDoublePref(kProfileProxyManualGeoLongitude, 0.0);
  registry->RegisterStringPref(kProfileProxyDerivedGeoCountryCode,
                               std::string());
  registry->RegisterStringPref(kProfileProxyDerivedGeoTimezone, std::string());
  registry->RegisterDoublePref(kProfileProxyDerivedGeoLatitude, 0.0);
  registry->RegisterDoublePref(kProfileProxyDerivedGeoLongitude, 0.0);
  registry->RegisterBooleanPref(kProfileProxyGeoLookupFailed, false);
}

}  // namespace fingerprint_browser::prefs
