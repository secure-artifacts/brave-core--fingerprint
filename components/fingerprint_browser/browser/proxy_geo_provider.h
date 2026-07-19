/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_FINGERPRINT_BROWSER_BROWSER_PROXY_GEO_PROVIDER_H_
#define BRAVE_COMPONENTS_FINGERPRINT_BROWSER_BROWSER_PROXY_GEO_PROVIDER_H_

#include <optional>
#include <string>
#include <string_view>

namespace fingerprint_browser {

enum class ProxyGeoProvider {
  kFreeIpApi,
  kIpWhoIs,
};

struct ProxyGeoLookupResult {
  std::string ip_address;
  std::string country_code;
  std::string country_name;
  std::string region_name;
  std::string city_name;
  std::string timezone;
  double latitude = 0.0;
  double longitude = 0.0;
  ProxyGeoProvider provider = ProxyGeoProvider::kFreeIpApi;
};

std::optional<ProxyGeoLookupResult> ParseProxyGeoResponse(
    ProxyGeoProvider provider,
    std::string_view response_body);
bool IsValidProxyGeoResult(const ProxyGeoLookupResult& result);
std::string_view ProxyGeoProviderName(ProxyGeoProvider provider);

}  // namespace fingerprint_browser

#endif  // BRAVE_COMPONENTS_FINGERPRINT_BROWSER_BROWSER_PROXY_GEO_PROVIDER_H_
