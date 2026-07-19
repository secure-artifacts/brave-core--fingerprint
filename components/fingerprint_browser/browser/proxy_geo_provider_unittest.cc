/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/fingerprint_browser/browser/proxy_geo_provider.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace fingerprint_browser {

TEST(ProxyGeoProviderTest, ParsesUnambiguousFreeIpApiResponse) {
  const auto result = ParseProxyGeoResponse(ProxyGeoProvider::kFreeIpApi,
                                            R"({
        "ipAddress":"1.1.1.1",
        "latitude":-33.8688,
        "longitude":151.2093,
        "countryName":"Australia",
        "countryCode":"AU",
        "timeZones":["Australia/Sydney"],
        "cityName":"Sydney",
        "regionName":"New South Wales"
      })");

  ASSERT_TRUE(result);
  EXPECT_EQ("1.1.1.1", result->ip_address);
  EXPECT_EQ("AU", result->country_code);
  EXPECT_EQ("Australia/Sydney", result->timezone);
  EXPECT_EQ(ProxyGeoProvider::kFreeIpApi, result->provider);
}

TEST(ProxyGeoProviderTest, RejectsAmbiguousFreeIpApiTimezoneList) {
  EXPECT_FALSE(ParseProxyGeoResponse(ProxyGeoProvider::kFreeIpApi,
                                     R"({
        "ipAddress":"8.8.8.8",
        "latitude":37.386,
        "longitude":-122.084,
        "countryName":"United States",
        "countryCode":"US",
        "timeZones":["America/New_York","America/Los_Angeles"],
        "cityName":"Mountain View",
        "regionName":"California"
      })"));
}

TEST(ProxyGeoProviderTest, ParsesIpWhoIsResponse) {
  const auto result = ParseProxyGeoResponse(ProxyGeoProvider::kIpWhoIs,
                                            R"({
        "ip":"8.8.4.4",
        "success":true,
        "country":"United States",
        "country_code":"US",
        "region":"California",
        "city":"Mountain View",
        "latitude":37.3860517,
        "longitude":-122.0838511,
        "timezone":{"id":"America/Los_Angeles"}
      })");

  ASSERT_TRUE(result);
  EXPECT_EQ("8.8.4.4", result->ip_address);
  EXPECT_EQ("US", result->country_code);
  EXPECT_EQ("America/Los_Angeles", result->timezone);
  EXPECT_EQ(ProxyGeoProvider::kIpWhoIs, result->provider);
}

TEST(ProxyGeoProviderTest, RejectsApplicationAndInvalidGeoErrors) {
  EXPECT_FALSE(ParseProxyGeoResponse(
      ProxyGeoProvider::kIpWhoIs,
      R"({"ip":"8.8.4.4","success":false,"message":"Rate limit"})"));
  EXPECT_FALSE(ParseProxyGeoResponse(ProxyGeoProvider::kIpWhoIs,
                                     R"({
        "ip":"127.0.0.1",
        "success":true,
        "country":"Local",
        "country_code":"XX",
        "region":"Local",
        "city":"Local",
        "latitude":91,
        "longitude":181,
        "timezone":{"id":"Invalid/Timezone"}
                                     })"));
}

TEST(ProxyGeoProviderTest, RejectsUnknownCountryCode) {
  EXPECT_FALSE(ParseProxyGeoResponse(ProxyGeoProvider::kIpWhoIs,
                                     R"({
        "ip":"8.8.4.4",
        "success":true,
        "country":"Unknown",
        "country_code":"ZZ",
        "region":"Unknown",
        "city":"Unknown",
        "latitude":37.3860517,
        "longitude":-122.0838511,
        "timezone":{"id":"America/Los_Angeles"}
      })"));
}

}  // namespace fingerprint_browser
