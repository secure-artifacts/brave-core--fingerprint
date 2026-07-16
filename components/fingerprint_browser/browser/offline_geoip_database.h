/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_FINGERPRINT_BROWSER_BROWSER_OFFLINE_GEOIP_DATABASE_H_
#define BRAVE_COMPONENTS_FINGERPRINT_BROWSER_BROWSER_OFFLINE_GEOIP_DATABASE_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/files/file_path.h"
#include "base/no_destructor.h"

namespace fingerprint_browser {

struct OfflineGeoIpResult {
  std::string country_code;
  std::string timezone;
  std::string accept_languages;
  double latitude = 0.0;
  double longitude = 0.0;
};

// Reads the offline geo data component. The component contains DB-IP Lite's
// city MMDB plus GeoNames-derived timezone and locale indexes.
class OfflineGeoIpDatabase {
 public:
  static OfflineGeoIpDatabase* GetInstance();

  OfflineGeoIpDatabase(const OfflineGeoIpDatabase&) = delete;
  OfflineGeoIpDatabase& operator=(const OfflineGeoIpDatabase&) = delete;

  void SetDatabaseDirectory(base::FilePath directory);
  std::optional<OfflineGeoIpResult> Lookup(std::string_view ip_address);

 private:
  friend class base::NoDestructor<OfflineGeoIpDatabase>;
  OfflineGeoIpDatabase();
  ~OfflineGeoIpDatabase();
};

}  // namespace fingerprint_browser

#endif  // BRAVE_COMPONENTS_FINGERPRINT_BROWSER_BROWSER_OFFLINE_GEOIP_DATABASE_H_
