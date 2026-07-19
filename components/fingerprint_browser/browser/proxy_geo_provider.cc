/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/fingerprint_browser/browser/proxy_geo_provider.h"

#include <memory>

#include "base/compiler_specific.h"
#include "base/json/json_reader.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "net/base/ip_address.h"
#include "third_party/icu/source/common/unicode/uloc.h"
#include "third_party/icu/source/common/unicode/unistr.h"
#include "third_party/icu/source/i18n/unicode/timezone.h"

namespace fingerprint_browser {
namespace {

bool IsCountryCode(std::string_view country_code) {
  if (country_code.size() != 2 || !base::IsAsciiAlpha(country_code[0]) ||
      !base::IsAsciiAlpha(country_code[1])) {
    return false;
  }
  const std::string normalized = base::ToUpperASCII(country_code);
  for (const char* const* country = uloc_getISOCountries(); *country;
       UNSAFE_TODO(++country)) {
    if (normalized == *country) {
      return true;
    }
  }
  return false;
}

bool IsIanaTimezone(std::string_view timezone_id) {
  if (timezone_id.empty()) {
    return false;
  }
  std::unique_ptr<icu::TimeZone> timezone(
      icu::TimeZone::createTimeZone(icu::UnicodeString::fromUTF8(timezone_id)));
  return timezone && *timezone != icu::TimeZone::getUnknown();
}

std::optional<ProxyGeoLookupResult> ParseFreeIpApi(
    const base::DictValue& response) {
  const std::string* ip_address = response.FindString("ipAddress");
  const std::string* country_code = response.FindString("countryCode");
  const std::string* country_name = response.FindString("countryName");
  const std::string* region_name = response.FindString("regionName");
  const std::string* city_name = response.FindString("cityName");
  const base::ListValue* timezones = response.FindList("timeZones");
  const std::optional<double> latitude = response.FindDouble("latitude");
  const std::optional<double> longitude = response.FindDouble("longitude");

  // FreeIPAPI returns every timezone used by a country. It is usable only when
  // that list is unambiguous; otherwise the caller falls back to IPWHOIS.
  if (!ip_address || !country_code || !country_name || !region_name ||
      !city_name || !timezones || timezones->size() != 1 || !latitude ||
      !longitude || !(*timezones)[0].is_string()) {
    return std::nullopt;
  }

  ProxyGeoLookupResult result;
  result.ip_address = *ip_address;
  result.country_code = base::ToUpperASCII(*country_code);
  result.country_name = *country_name;
  result.region_name = *region_name;
  result.city_name = *city_name;
  result.timezone = (*timezones)[0].GetString();
  result.latitude = *latitude;
  result.longitude = *longitude;
  result.provider = ProxyGeoProvider::kFreeIpApi;
  return result;
}

std::optional<ProxyGeoLookupResult> ParseIpWhoIs(
    const base::DictValue& response) {
  if (!response.FindBool("success").value_or(false)) {
    return std::nullopt;
  }

  const std::string* ip_address = response.FindString("ip");
  const std::string* country_code = response.FindString("country_code");
  const std::string* country_name = response.FindString("country");
  const std::string* region_name = response.FindString("region");
  const std::string* city_name = response.FindString("city");
  const std::string* timezone = response.FindStringByDottedPath("timezone.id");
  const std::optional<double> latitude = response.FindDouble("latitude");
  const std::optional<double> longitude = response.FindDouble("longitude");
  if (!ip_address || !country_code || !country_name || !region_name ||
      !city_name || !timezone || !latitude || !longitude) {
    return std::nullopt;
  }

  ProxyGeoLookupResult result;
  result.ip_address = *ip_address;
  result.country_code = base::ToUpperASCII(*country_code);
  result.country_name = *country_name;
  result.region_name = *region_name;
  result.city_name = *city_name;
  result.timezone = *timezone;
  result.latitude = *latitude;
  result.longitude = *longitude;
  result.provider = ProxyGeoProvider::kIpWhoIs;
  return result;
}

}  // namespace

std::optional<ProxyGeoLookupResult> ParseProxyGeoResponse(
    ProxyGeoProvider provider,
    std::string_view response_body) {
  std::optional<base::DictValue> response =
      base::JSONReader::ReadDict(response_body, base::JSON_PARSE_RFC);
  if (!response) {
    return std::nullopt;
  }

  std::optional<ProxyGeoLookupResult> result;
  switch (provider) {
    case ProxyGeoProvider::kFreeIpApi:
      result = ParseFreeIpApi(*response);
      break;
    case ProxyGeoProvider::kIpWhoIs:
      result = ParseIpWhoIs(*response);
      break;
  }
  if (!result || !IsValidProxyGeoResult(*result)) {
    return std::nullopt;
  }
  return result;
}

bool IsValidProxyGeoResult(const ProxyGeoLookupResult& result) {
  const std::optional<net::IPAddress> ip_address =
      net::IPAddress::FromIPLiteral(result.ip_address);
  return ip_address && ip_address->IsPubliclyRoutable() &&
         IsCountryCode(result.country_code) &&
         IsIanaTimezone(result.timezone) && result.latitude >= -90.0 &&
         result.latitude <= 90.0 && result.longitude >= -180.0 &&
         result.longitude <= 180.0;
}

std::string_view ProxyGeoProviderName(ProxyGeoProvider provider) {
  switch (provider) {
    case ProxyGeoProvider::kFreeIpApi:
      return "freeipapi";
    case ProxyGeoProvider::kIpWhoIs:
      return "ipwhois";
  }
}

}  // namespace fingerprint_browser
