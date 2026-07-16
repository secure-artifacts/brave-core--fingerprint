/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/fingerprint_browser/browser/offline_geoip_database.h"

#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/synchronization/lock.h"
#include "brave/third_party/libmaxminddb/include/maxminddb.h"

namespace fingerprint_browser {

namespace {

constexpr char kDatabaseFileName[] = "dbip-city-lite.mmdb";
constexpr char kTimezoneIndexFileName[] = "geonames-city-timezones.tsv";
constexpr char kLanguageIndexFileName[] = "geonames-country-languages.tsv";
constexpr double kRadiansPerDegree = 0.017453292519943295;

struct CityTimezone {
  std::string country_code;
  double latitude = 0.0;
  double longitude = 0.0;
  std::string timezone;
};

struct GeoIpData {
  base::FilePath directory;
  std::vector<CityTimezone> city_timezones;
  std::map<std::string, std::string> country_languages;
  base::Lock lock;
};

GeoIpData& GetGeoIpData() {
  static base::NoDestructor<GeoIpData> data;
  return *data;
}

std::optional<std::string> GetStringValue(MMDB_entry_s entry,
                                          const char* first,
                                          const char* second) {
  MMDB_entry_data_s value;
  if (MMDB_get_value(&entry, &value, first, second, nullptr) != MMDB_SUCCESS ||
      !value.has_data || value.type != MMDB_DATA_TYPE_UTF8_STRING) {
    return std::nullopt;
  }
  return std::string(value.utf8_string, value.data_size);
}

std::optional<double> GetDoubleValue(MMDB_entry_s entry,
                                     const char* first,
                                     const char* second) {
  MMDB_entry_data_s value;
  if (MMDB_get_value(&entry, &value, first, second, nullptr) != MMDB_SUCCESS ||
      !value.has_data || value.type != MMDB_DATA_TYPE_DOUBLE) {
    return std::nullopt;
  }
  return value.double_value;
}

bool IsValidCountryCode(std::string_view country_code) {
  return country_code.size() == 2 && base::IsAsciiAlpha(country_code[0]) &&
         base::IsAsciiAlpha(country_code[1]);
}

std::vector<CityTimezone> ReadCityTimezones(const base::FilePath& path) {
  std::string contents;
  if (!base::ReadFileToString(path, &contents)) {
    return {};
  }

  std::vector<CityTimezone> timezones;
  for (std::string_view line : base::SplitStringPiece(
           contents, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    const auto fields = base::SplitStringPiece(
        line, "\t", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
    if (fields.size() != 4 || !IsValidCountryCode(fields[0])) {
      continue;
    }
    double latitude = 0.0;
    double longitude = 0.0;
    if (!base::StringToDouble(fields[1], &latitude) ||
        !base::StringToDouble(fields[2], &longitude) ||
        !std::isfinite(latitude) || !std::isfinite(longitude) ||
        latitude < -90.0 || latitude > 90.0 || longitude < -180.0 ||
        longitude > 180.0 || fields[3].empty()) {
      continue;
    }
    timezones.push_back({base::ToUpperASCII(fields[0]), latitude, longitude,
                         std::string(fields[3])});
  }
  return timezones;
}

std::map<std::string, std::string> ReadCountryLanguages(
    const base::FilePath& path) {
  std::string contents;
  if (!base::ReadFileToString(path, &contents)) {
    return {};
  }

  std::map<std::string, std::string> languages;
  for (std::string_view line : base::SplitStringPiece(
           contents, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    const auto fields = base::SplitStringPiece(
        line, "\t", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
    if (fields.size() == 2 && IsValidCountryCode(fields[0]) &&
        !fields[1].empty()) {
      languages.emplace(base::ToUpperASCII(fields[0]), std::string(fields[1]));
    }
  }
  return languages;
}

bool LoadSupportingIndexes(const base::FilePath& directory) {
  GeoIpData& data = GetGeoIpData();
  base::AutoLock lock(data.lock);
  if (data.directory == directory && !data.city_timezones.empty() &&
      !data.country_languages.empty()) {
    return true;
  }

  auto city_timezones =
      ReadCityTimezones(directory.AppendASCII(kTimezoneIndexFileName));
  auto country_languages =
      ReadCountryLanguages(directory.AppendASCII(kLanguageIndexFileName));
  if (city_timezones.empty() || country_languages.empty()) {
    return false;
  }
  data.city_timezones = std::move(city_timezones);
  data.country_languages = std::move(country_languages);
  return true;
}

std::optional<std::string> FindTimezone(std::string_view country_code,
                                        double latitude,
                                        double longitude) {
  GeoIpData& data = GetGeoIpData();
  base::AutoLock lock(data.lock);
  const CityTimezone* closest = nullptr;
  double closest_distance = std::numeric_limits<double>::infinity();
  const double latitude_scale = std::cos(latitude * kRadiansPerDegree);
  for (const CityTimezone& candidate : data.city_timezones) {
    if (candidate.country_code != country_code) {
      continue;
    }
    const double latitude_distance = candidate.latitude - latitude;
    const double longitude_distance =
        (candidate.longitude - longitude) * latitude_scale;
    const double distance = latitude_distance * latitude_distance +
                            longitude_distance * longitude_distance;
    if (distance < closest_distance) {
      closest = &candidate;
      closest_distance = distance;
    }
  }
  if (!closest) {
    return std::nullopt;
  }
  return closest->timezone;
}

std::optional<std::string> FindAcceptLanguages(std::string_view country_code) {
  GeoIpData& data = GetGeoIpData();
  base::AutoLock lock(data.lock);
  const auto it = data.country_languages.find(std::string(country_code));
  if (it == data.country_languages.end()) {
    return std::nullopt;
  }
  return it->second;
}

}  // namespace

OfflineGeoIpDatabase::OfflineGeoIpDatabase() = default;
OfflineGeoIpDatabase::~OfflineGeoIpDatabase() = default;

// static
OfflineGeoIpDatabase* OfflineGeoIpDatabase::GetInstance() {
  static base::NoDestructor<OfflineGeoIpDatabase> database;
  return database.get();
}

void OfflineGeoIpDatabase::SetDatabaseDirectory(base::FilePath directory) {
  GeoIpData& data = GetGeoIpData();
  base::AutoLock lock(data.lock);
  data.directory = std::move(directory);
  data.city_timezones.clear();
  data.country_languages.clear();
}

std::optional<OfflineGeoIpResult> OfflineGeoIpDatabase::Lookup(
    std::string_view ip_address) {
  base::FilePath directory;
  {
    GeoIpData& data = GetGeoIpData();
    base::AutoLock lock(data.lock);
    directory = data.directory;
  }
  if (directory.empty() || !LoadSupportingIndexes(directory)) {
    return std::nullopt;
  }

  MMDB_s database = {};
  const base::FilePath database_path = directory.AppendASCII(kDatabaseFileName);
  if (MMDB_open(database_path.AsUTF8Unsafe().c_str(), MMDB_MODE_MMAP,
                &database) != MMDB_SUCCESS) {
    return std::nullopt;
  }

  int gai_error = 0;
  int mmdb_error = MMDB_SUCCESS;
  const MMDB_lookup_result_s lookup = MMDB_lookup_string(
      &database, std::string(ip_address).c_str(), &gai_error, &mmdb_error);
  if (gai_error != 0 || mmdb_error != MMDB_SUCCESS || !lookup.found_entry) {
    MMDB_close(&database);
    return std::nullopt;
  }

  auto country_code = GetStringValue(lookup.entry, "country", "iso_code");
  auto latitude = GetDoubleValue(lookup.entry, "location", "latitude");
  auto longitude = GetDoubleValue(lookup.entry, "location", "longitude");
  if (!country_code || !latitude || !longitude ||
      !IsValidCountryCode(*country_code) || !std::isfinite(*latitude) ||
      !std::isfinite(*longitude) || *latitude < -90.0 || *latitude > 90.0 ||
      *longitude < -180.0 || *longitude > 180.0) {
    MMDB_close(&database);
    return std::nullopt;
  }

  *country_code = base::ToUpperASCII(*country_code);
  auto timezone = GetStringValue(lookup.entry, "location", "time_zone");
  if (!timezone || timezone->empty()) {
    timezone = FindTimezone(*country_code, *latitude, *longitude);
  }
  const auto accept_languages = FindAcceptLanguages(*country_code);
  MMDB_close(&database);
  if (!timezone || timezone->empty() || !accept_languages) {
    return std::nullopt;
  }
  return OfflineGeoIpResult{*country_code, *timezone, *accept_languages,
                            *latitude, *longitude};
}

}  // namespace fingerprint_browser
