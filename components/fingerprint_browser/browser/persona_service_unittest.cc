/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/fingerprint_browser/browser/persona_service.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "base/base_paths.h"
#include "base/check.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "brave/components/fingerprint_browser/browser/offline_geoip_database.h"
#include "brave/components/fingerprint_browser/browser/persona_generator.h"
#include "brave/components/fingerprint_browser/browser/pref_names.h"
#include "brave/components/fingerprint_browser/browser/profile_proxy_config.h"
#include "brave/components/fingerprint_browser/browser/truth_pool.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "components/proxy_config/proxy_config_pref_names.h"
#include "net/base/proxy_server.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace fingerprint_browser {

namespace {

constexpr char kWebRTCIPHandlingPolicyPref[] = "webrtc.ip_handling_policy";
constexpr char kWebRTCIPHandlingDefault[] = "default";
constexpr char kWebRTCIPHandlingDefaultPublicInterfaceOnly[] =
    "default_public_interface_only";
constexpr char kWebRTCIPHandlingDisableNonProxiedUdp[] =
    "disable_non_proxied_udp";
constexpr char kAcceptLanguagesPref[] = "intl.accept_languages";
constexpr char kAcceptLanguagesDefault[] = "en-US,en";
constexpr char kAcceptLanguagesJapanese[] = "ja-JP,ja";
constexpr char kAcceptLanguagesBritish[] = "en-GB,en";

void RegisterPrefs(TestingPrefServiceSimple* prefs) {
  PersonaService::RegisterProfilePrefs(prefs->registry());
}

void RegisterWebRTCPrefs(TestingPrefServiceSimple* prefs) {
  prefs->registry()->RegisterStringPref(kWebRTCIPHandlingPolicyPref,
                                        kWebRTCIPHandlingDefault);
}

void RegisterAcceptLanguagePrefs(TestingPrefServiceSimple* prefs) {
  prefs->registry()->RegisterStringPref(kAcceptLanguagesPref,
                                        kAcceptLanguagesDefault);
}

void RegisterProxyConflictPrefs(TestingPrefServiceSimple* prefs) {
  prefs->registry()->RegisterDictionaryPref(proxy_config::prefs::kProxy);
  prefs->registry()->RegisterListPref(proxy_config::prefs::kProxyOverrideRules);
}

base::FilePath GetGeoIpTestDataDirectory() {
  base::FilePath source_root;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &source_root));
  const base::FilePath directory = source_root.AppendASCII("brave")
                                       .AppendASCII("components")
                                       .AppendASCII("fingerprint_browser")
                                       .AppendASCII("browser")
                                       .AppendASCII("test")
                                       .AppendASCII("data");
  CHECK(base::DirectoryExists(directory)) << directory;
  return directory;
}

void SetProfileProxyPrefs(TestingPrefServiceSimple* prefs,
                          std::string_view scheme,
                          std::string_view host,
                          int port,
                          std::string_view username = std::string_view(),
                          std::string_view password = std::string_view()) {
  prefs->SetString(prefs::kProfileProxyScheme, std::string(scheme));
  prefs->SetString(prefs::kProfileProxyHost, std::string(host));
  prefs->SetInteger(prefs::kProfileProxyPort, port);
  prefs->SetString(prefs::kProfileProxyUsername, std::string(username));
  prefs->SetString(prefs::kProfileProxyPassword, std::string(password));
}

bool BrandsEqual(const std::vector<UserAgentBrand>& lhs,
                 const std::vector<UserAgentBrand>& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (size_t i = 0; i < lhs.size(); ++i) {
    if (lhs[i].brand != rhs[i].brand || lhs[i].version != rhs[i].version) {
      return false;
    }
  }
  return true;
}

bool UserAgentMetadataEqual(const UserAgentMetadata& lhs,
                            const UserAgentMetadata& rhs) {
  return lhs.platform == rhs.platform &&
         lhs.platform_version == rhs.platform_version &&
         lhs.architecture == rhs.architecture && lhs.bitness == rhs.bitness &&
         lhs.full_version == rhs.full_version && lhs.mobile == rhs.mobile &&
         BrandsEqual(lhs.brands, rhs.brands) &&
         BrandsEqual(lhs.full_version_list, rhs.full_version_list);
}

}  // namespace

TEST(PersonaGeneratorTest, GeneratedPersonaIsConsistent) {
  std::string error;
  auto persona =
      GeneratePersonaFromSeed(GetDefaultTruthPool(), "profile-a", &error);
  ASSERT_TRUE(persona) << error;
  EXPECT_TRUE(IsPersonaValid(*persona));

  if (persona->os == PersonaOS::kWindows) {
    EXPECT_EQ("Windows", persona->ua_metadata.platform);
    EXPECT_NE(std::string::npos, persona->webgl.renderer.find("Direct3D"));
  } else {
    EXPECT_EQ("macOS", persona->ua_metadata.platform);
    EXPECT_NE(std::string::npos, persona->webgl.renderer.find("Metal"));
  }

  EXPECT_LE(persona->screen.avail_width, persona->screen.width);
  EXPECT_LE(persona->screen.avail_height, persona->screen.height);
  EXPECT_EQ(0, persona->max_touch_points);
  EXPECT_FALSE(persona->media_devices.empty());
  EXPECT_EQ(
      1u, std::ranges::count_if(persona->speech_voices, [](const auto& voice) {
        return voice.is_default;
      }));
}

TEST(PersonaGeneratorTest, GeneratedPersonaFieldsComeFromTruthPool) {
  const TruthPool pool = GetDefaultTruthPool();

  std::string error;
  auto persona = GeneratePersonaFromSeed(pool, "profile-a", &error);
  ASSERT_TRUE(persona) << error;

  EXPECT_TRUE(std::ranges::any_of(pool.user_agents, [&](const auto& entry) {
    return entry.os == persona->os && entry.user_agent == persona->user_agent &&
           entry.platform == persona->platform &&
           UserAgentMetadataEqual(entry.metadata, persona->ua_metadata);
  }));
  EXPECT_TRUE(std::ranges::any_of(pool.renderers, [&](const auto& entry) {
    return entry.os == persona->os &&
           entry.webgl.vendor == persona->webgl.vendor &&
           entry.webgl.renderer == persona->webgl.renderer &&
           entry.webgpu.vendor == persona->webgpu.vendor &&
           entry.webgpu.architecture == persona->webgpu.architecture &&
           entry.webgpu.device == persona->webgpu.device &&
           entry.webgpu.description == persona->webgpu.description;
  }));
  EXPECT_TRUE(std::ranges::any_of(pool.screens, [&](const auto& entry) {
    return entry.os == persona->os &&
           entry.screen.width == persona->screen.width &&
           entry.screen.height == persona->screen.height &&
           entry.screen.avail_width == persona->screen.avail_width &&
           entry.screen.avail_height == persona->screen.avail_height &&
           entry.screen.color_depth == persona->screen.color_depth &&
           entry.screen.device_scale_factor ==
               persona->screen.device_scale_factor &&
           entry.screen.window_x == persona->screen.window_x &&
           entry.screen.window_y == persona->screen.window_y &&
           entry.hardware_concurrency == persona->hardware_concurrency &&
           entry.device_memory_gb == persona->device_memory_gb &&
           entry.max_touch_points == persona->max_touch_points;
  }));
  EXPECT_TRUE(std::ranges::any_of(pool.font_sets, [&](const auto& entry) {
    return entry.os == persona->os && entry.locale == persona->locale &&
           entry.fonts == persona->fonts;
  }));
  EXPECT_TRUE(std::ranges::any_of(pool.locales, [&](const auto& entry) {
    return entry.locale == persona->locale &&
           entry.languages == persona->languages &&
           entry.accept_language == persona->accept_language;
  }));
  EXPECT_TRUE(std::ranges::any_of(pool.noise_seeds, [&](const auto& entry) {
    return entry.canvas_noise_seed == persona->canvas_noise_seed &&
           entry.audio_noise_seed == persona->audio_noise_seed;
  }));
  EXPECT_TRUE(
      std::ranges::any_of(pool.media_device_sets, [&](const auto& entry) {
        return entry.os == persona->os &&
               entry.devices == persona->media_devices;
      }));
  EXPECT_TRUE(
      std::ranges::any_of(pool.speech_voice_sets, [&](const auto& entry) {
        return entry.os == persona->os && entry.locale == persona->locale &&
               entry.voices == persona->speech_voices;
      }));
}

TEST(PersonaGeneratorTest, FailsWhenTruthPoolMissingRequiredDimension) {
  TruthPool pool = GetDefaultTruthPool();
  pool.renderers.clear();

  std::string error;
  auto persona = GeneratePersonaFromSeed(pool, "profile-a", &error);
  EXPECT_FALSE(persona);
  EXPECT_NE(std::string::npos, error.find("renderer"));
}

TEST(PersonaSerializationTest, RoundTripsCurrentSchema) {
  std::string error;
  auto persona =
      GeneratePersonaFromSeed(GetDefaultTruthPool(), "profile-a", &error);
  ASSERT_TRUE(persona) << error;

  auto parsed = PersonaFromValue(PersonaToValue(*persona));
  ASSERT_TRUE(parsed);
  EXPECT_EQ(persona->persona_id, parsed->persona_id);
  EXPECT_EQ(persona->user_agent, parsed->user_agent);
  EXPECT_EQ(persona->webgl.renderer, parsed->webgl.renderer);
  EXPECT_EQ(persona->fonts, parsed->fonts);
  EXPECT_EQ(persona->media_devices, parsed->media_devices);
  EXPECT_EQ(persona->speech_voices, parsed->speech_voices);
}

TEST(PersonaServiceTest, PersistsPersonaAcrossServiceRestart) {
  TestingPrefServiceSimple prefs;
  RegisterPrefs(&prefs);

  PersonaService first_service(&prefs, "profile-a");
  ASSERT_TRUE(first_service.has_persona()) << first_service.last_error();
  const std::string first_id = first_service.GetPersona().persona_id;

  PersonaService restarted_service(&prefs, "profile-b");
  ASSERT_TRUE(restarted_service.has_persona())
      << restarted_service.last_error();
  EXPECT_EQ(first_id, restarted_service.GetPersona().persona_id);
}

TEST(PersonaServiceTest, DifferentProfilesGetDifferentPersonaIds) {
  TestingPrefServiceSimple prefs_a;
  RegisterPrefs(&prefs_a);
  TestingPrefServiceSimple prefs_b;
  RegisterPrefs(&prefs_b);

  PersonaService service_a(&prefs_a, "profile-a");
  PersonaService service_b(&prefs_b, "profile-b");

  ASSERT_TRUE(service_a.has_persona()) << service_a.last_error();
  ASSERT_TRUE(service_b.has_persona()) << service_b.last_error();
  EXPECT_NE(service_a.GetPersona().persona_id,
            service_b.GetPersona().persona_id);
}

TEST(PersonaServiceTest, DamagedPrefRegeneratesCurrentSchema) {
  TestingPrefServiceSimple prefs;
  RegisterPrefs(&prefs);

  base::DictValue damaged;
  damaged.Set("schema_version", kCurrentPersonaSchemaVersion);
  damaged.Set("persona_id", "missing-required-fields");
  prefs.SetDict(prefs::kPersona, std::move(damaged));

  PersonaService service(&prefs, "profile-a");
  ASSERT_TRUE(service.has_persona()) << service.last_error();
  EXPECT_TRUE(IsPersonaValid(service.GetPersona()));

  auto parsed = PersonaFromValue(prefs.GetDict(prefs::kPersona));
  ASSERT_TRUE(parsed);
  EXPECT_EQ(service.GetPersona().persona_id, parsed->persona_id);
}

TEST(PersonaServiceTest, OldSchemaPrefRegeneratesCurrentSchema) {
  TestingPrefServiceSimple prefs;
  RegisterPrefs(&prefs);

  std::string error;
  auto old_persona =
      GeneratePersonaFromSeed(GetDefaultTruthPool(), "profile-b", &error);
  ASSERT_TRUE(old_persona) << error;
  base::DictValue old_pref = PersonaToValue(*old_persona);
  old_pref.Set("schema_version", kCurrentPersonaSchemaVersion - 1);
  prefs.SetDict(prefs::kPersona, std::move(old_pref));

  PersonaService service(&prefs, "profile-a");
  ASSERT_TRUE(service.has_persona()) << service.last_error();
  EXPECT_EQ(kCurrentPersonaSchemaVersion, service.GetPersona().schema_version);
  EXPECT_NE(old_persona->persona_id, service.GetPersona().persona_id);

  auto parsed = PersonaFromValue(prefs.GetDict(prefs::kPersona));
  ASSERT_TRUE(parsed);
  EXPECT_EQ(service.GetPersona().persona_id, parsed->persona_id);
}

TEST(ProfileProxyConfigTest, ProfileProxyDefaultsOff) {
  TestingPrefServiceSimple prefs;
  RegisterPrefs(&prefs);

  EXPECT_FALSE(IsProfileProxyEnabled(prefs));
  EXPECT_FALSE(ShouldUseProfileProxy(prefs));
  EXPECT_FALSE(GetProfileProxyServerFromPrefs(prefs));
}

TEST(ProfileProxyConfigTest, ProfileProxyReadsEnabledPref) {
  TestingPrefServiceSimple prefs;
  RegisterPrefs(&prefs);

  prefs.SetBoolean(prefs::kProfileProxyEnabled, true);
  SetProfileProxyPrefs(&prefs, prefs::kProfileProxySchemeHttp, "proxy.example",
                       8080);

  EXPECT_TRUE(IsProfileProxyEnabled(prefs));
  EXPECT_TRUE(ShouldUseProfileProxy(prefs));
  EXPECT_EQ(ProfileProxyConfigConflict::kNone,
            GetProfileProxyConfigConflict(prefs));
  EXPECT_TRUE(
      ProfileProxyConfigConflictWarning(ProfileProxyConfigConflict::kNone)
          .empty());
}

TEST(ProfileProxyConfigTest, EnabledWithoutServerDoesNotTakeOver) {
  TestingPrefServiceSimple prefs;
  RegisterPrefs(&prefs);

  prefs.SetBoolean(prefs::kProfileProxyEnabled, true);

  EXPECT_TRUE(IsProfileProxyEnabled(prefs));
  EXPECT_FALSE(GetProfileProxyServerFromPrefs(prefs));
  EXPECT_FALSE(ShouldUseProfileProxy(prefs));
}

TEST(ProfileProxyConfigTest, BuildsHttpProxyWithCredentials) {
  TestingPrefServiceSimple prefs;
  RegisterPrefs(&prefs);

  SetProfileProxyPrefs(&prefs, prefs::kProfileProxySchemeHttp, "proxy.example",
                       8080, "user", "pass");

  auto proxy_server = GetProfileProxyServerFromPrefs(prefs);
  ASSERT_TRUE(proxy_server);
  EXPECT_EQ(net::ProxyServer::SCHEME_HTTP, proxy_server->scheme());
  EXPECT_EQ("proxy.example", proxy_server->host_port_pair().host());
  EXPECT_EQ(8080, proxy_server->host_port_pair().port());
  EXPECT_EQ("user", proxy_server->host_port_pair().username());
  EXPECT_EQ("pass", proxy_server->host_port_pair().password());
}

TEST(ProfileProxyConfigTest, BuildsHttpProxyWithIPv6Host) {
  TestingPrefServiceSimple prefs;
  RegisterPrefs(&prefs);

  SetProfileProxyPrefs(&prefs, prefs::kProfileProxySchemeHttp, "2001:218::1",
                       8080);

  auto proxy_server = GetProfileProxyServerFromPrefs(prefs);
  ASSERT_TRUE(proxy_server);
  EXPECT_EQ("2001:218::1", proxy_server->host_port_pair().host());
  EXPECT_EQ(8080, proxy_server->host_port_pair().port());
}

TEST(ProfileProxyConfigTest, BuildsSocks5ProxyWithCredentials) {
  TestingPrefServiceSimple prefs;
  RegisterPrefs(&prefs);

  SetProfileProxyPrefs(&prefs, prefs::kProfileProxySchemeSocks5, "127.0.0.1",
                       1080, "socks-user", "socks-pass");

  auto proxy_server = GetProfileProxyServerFromPrefs(prefs);
  ASSERT_TRUE(proxy_server);
  EXPECT_EQ(net::ProxyServer::SCHEME_SOCKS5, proxy_server->scheme());
  EXPECT_EQ("127.0.0.1", proxy_server->host_port_pair().host());
  EXPECT_EQ(1080, proxy_server->host_port_pair().port());
  EXPECT_EQ("socks-user", proxy_server->host_port_pair().username());
  EXPECT_EQ("socks-pass", proxy_server->host_port_pair().password());
}

TEST(ProfileProxyConfigTest, RejectsInvalidProxyServerPrefs) {
  TestingPrefServiceSimple prefs;
  RegisterPrefs(&prefs);

  SetProfileProxyPrefs(&prefs, "ftp", "proxy.example", 8080);
  EXPECT_FALSE(GetProfileProxyServerFromPrefs(prefs));

  SetProfileProxyPrefs(&prefs, prefs::kProfileProxySchemeHttp, "proxy.example",
                       0);
  EXPECT_FALSE(GetProfileProxyServerFromPrefs(prefs));

  SetProfileProxyPrefs(&prefs, prefs::kProfileProxySchemeHttp,
                       std::string_view(), 8080);
  EXPECT_FALSE(GetProfileProxyServerFromPrefs(prefs));
}

TEST(ProfileProxyConfigTest, PolicyProxyTakesPriority) {
  TestingPrefServiceSimple prefs;
  RegisterPrefs(&prefs);
  RegisterProxyConflictPrefs(&prefs);

  prefs.SetBoolean(prefs::kProfileProxyEnabled, true);
  SetProfileProxyPrefs(&prefs, prefs::kProfileProxySchemeHttp, "proxy.example",
                       8080);
  prefs.SetManagedPref(proxy_config::prefs::kProxy, base::DictValue());

  EXPECT_EQ(ProfileProxyConfigConflict::kPolicy,
            GetProfileProxyConfigConflict(prefs));
  EXPECT_FALSE(ShouldUseProfileProxy(prefs));
  EXPECT_EQ(
      "Enterprise policy controls this profile's proxy settings.",
      ProfileProxyConfigConflictWarning(ProfileProxyConfigConflict::kPolicy));
}

TEST(ProfileProxyConfigTest, ExtensionProxyTakesPriority) {
  TestingPrefServiceSimple prefs;
  RegisterPrefs(&prefs);
  RegisterProxyConflictPrefs(&prefs);

  prefs.SetBoolean(prefs::kProfileProxyEnabled, true);
  SetProfileProxyPrefs(&prefs, prefs::kProfileProxySchemeHttp, "proxy.example",
                       8080);
  prefs.SetExtensionPref(proxy_config::prefs::kProxy, base::DictValue());

  EXPECT_EQ(ProfileProxyConfigConflict::kExtension,
            GetProfileProxyConfigConflict(prefs));
  EXPECT_FALSE(ShouldUseProfileProxy(prefs));
  EXPECT_EQ("An extension controls this profile's proxy settings.",
            ProfileProxyConfigConflictWarning(
                ProfileProxyConfigConflict::kExtension));
}

TEST(ProfileProxyConfigTest, ProxyOverrideRulesConflictTakesPriority) {
  TestingPrefServiceSimple prefs;
  RegisterPrefs(&prefs);
  RegisterProxyConflictPrefs(&prefs);

  prefs.SetBoolean(prefs::kProfileProxyEnabled, true);
  SetProfileProxyPrefs(&prefs, prefs::kProfileProxySchemeHttp, "proxy.example",
                       8080);
  prefs.SetExtensionPref(proxy_config::prefs::kProxyOverrideRules,
                         base::ListValue());

  EXPECT_EQ(ProfileProxyConfigConflict::kExtension,
            GetProfileProxyConfigConflict(prefs));
  EXPECT_FALSE(ShouldUseProfileProxy(prefs));
}

TEST(ProfileProxyConfigTest, EnabledProfileProxyForcesWebRTCPolicy) {
  TestingPrefServiceSimple prefs;
  RegisterPrefs(&prefs);
  RegisterWebRTCPrefs(&prefs);

  prefs.SetString(kWebRTCIPHandlingPolicyPref,
                  kWebRTCIPHandlingDefaultPublicInterfaceOnly);
  prefs.SetBoolean(prefs::kProfileProxyEnabled, true);
  SetProfileProxyPrefs(&prefs, prefs::kProfileProxySchemeHttp, "proxy.example",
                       8080);

  SyncProfileProxyWebRTCPolicy(prefs);

  EXPECT_EQ(kWebRTCIPHandlingDisableNonProxiedUdp,
            prefs.GetString(kWebRTCIPHandlingPolicyPref));
  EXPECT_TRUE(
      prefs.GetBoolean(prefs::kProfileProxyHasSavedWebRTCIPHandlingPolicy));
  EXPECT_EQ(kWebRTCIPHandlingDefaultPublicInterfaceOnly,
            prefs.GetString(prefs::kProfileProxySavedWebRTCIPHandlingPolicy));
}

TEST(ProfileProxyConfigTest, DisabledProfileProxyRestoresWebRTCPolicy) {
  TestingPrefServiceSimple prefs;
  RegisterPrefs(&prefs);
  RegisterWebRTCPrefs(&prefs);

  prefs.SetString(kWebRTCIPHandlingPolicyPref,
                  kWebRTCIPHandlingDefaultPublicInterfaceOnly);
  prefs.SetBoolean(prefs::kProfileProxyEnabled, true);
  SetProfileProxyPrefs(&prefs, prefs::kProfileProxySchemeHttp, "proxy.example",
                       8080);
  SyncProfileProxyWebRTCPolicy(prefs);

  prefs.SetBoolean(prefs::kProfileProxyEnabled, false);
  SyncProfileProxyWebRTCPolicy(prefs);

  EXPECT_EQ(kWebRTCIPHandlingDefaultPublicInterfaceOnly,
            prefs.GetString(kWebRTCIPHandlingPolicyPref));
  EXPECT_FALSE(
      prefs.GetBoolean(prefs::kProfileProxyHasSavedWebRTCIPHandlingPolicy));
  EXPECT_TRUE(
      prefs.GetString(prefs::kProfileProxySavedWebRTCIPHandlingPolicy).empty());
}

TEST(ProfileProxyConfigTest, ProxyConflictDoesNotForceWebRTCPolicy) {
  TestingPrefServiceSimple prefs;
  RegisterPrefs(&prefs);
  RegisterProxyConflictPrefs(&prefs);
  RegisterWebRTCPrefs(&prefs);

  prefs.SetString(kWebRTCIPHandlingPolicyPref,
                  kWebRTCIPHandlingDefaultPublicInterfaceOnly);
  prefs.SetBoolean(prefs::kProfileProxyEnabled, true);
  SetProfileProxyPrefs(&prefs, prefs::kProfileProxySchemeHttp, "proxy.example",
                       8080);
  prefs.SetExtensionPref(proxy_config::prefs::kProxy, base::DictValue());

  SyncProfileProxyWebRTCPolicy(prefs);

  EXPECT_EQ(kWebRTCIPHandlingDefaultPublicInterfaceOnly,
            prefs.GetString(kWebRTCIPHandlingPolicyPref));
  EXPECT_FALSE(
      prefs.GetBoolean(prefs::kProfileProxyHasSavedWebRTCIPHandlingPolicy));
}

TEST(ProfileProxyConfigTest,
     EnabledProfileProxySavesAndAppliesAcceptLanguages) {
  TestingPrefServiceSimple prefs;
  RegisterPrefs(&prefs);
  RegisterAcceptLanguagePrefs(&prefs);

  prefs.SetString(kAcceptLanguagesPref, kAcceptLanguagesJapanese);
  prefs.SetBoolean(prefs::kProfileProxyEnabled, true);
  OfflineGeoIpDatabase::GetInstance()->SetDatabaseDirectory(
      GetGeoIpTestDataDirectory());
  SetProfileProxyPrefs(&prefs, prefs::kProfileProxySchemeHttp, "2001:218::1",
                       8080);

  SyncProfileProxyLanguage(prefs);

  EXPECT_EQ(kAcceptLanguagesJapanese, prefs.GetString(kAcceptLanguagesPref));
  EXPECT_TRUE(prefs.GetBoolean(prefs::kProfileProxyHasSavedAcceptLanguages));
  EXPECT_EQ(kAcceptLanguagesJapanese,
            prefs.GetString(prefs::kProfileProxySavedAcceptLanguages));
  EXPECT_EQ(kAcceptLanguagesJapanese,
            prefs.GetString(prefs::kProfileProxyDerivedAcceptLanguages));
}

TEST(ProfileProxyConfigTest, EnabledProfileProxyDerivesGeo) {
  TestingPrefServiceSimple prefs;
  RegisterPrefs(&prefs);
  RegisterAcceptLanguagePrefs(&prefs);

  prefs.SetString(kAcceptLanguagesPref, kAcceptLanguagesJapanese);
  prefs.SetBoolean(prefs::kProfileProxyEnabled, true);
  OfflineGeoIpDatabase::GetInstance()->SetDatabaseDirectory(
      GetGeoIpTestDataDirectory());
  SetProfileProxyPrefs(&prefs, prefs::kProfileProxySchemeHttp, "2001:218::1",
                       8080);

  SyncProfileProxyLanguage(prefs);

  const auto geo = GetProfileProxyGeoForPrefs(prefs);
  ASSERT_TRUE(geo);
  EXPECT_EQ("JP", geo->country_code);
  EXPECT_EQ("Asia/Tokyo", geo->timezone);
  EXPECT_DOUBLE_EQ(35.68536, geo->latitude);
  EXPECT_DOUBLE_EQ(139.75309, geo->longitude);
  EXPECT_EQ(kAcceptLanguagesJapanese, geo->accept_languages);
  EXPECT_TRUE(ProfileProxyGeoWarning(prefs).empty());
}

TEST(ProfileProxyConfigTest, DisabledProfileProxyRestoresAcceptLanguages) {
  TestingPrefServiceSimple prefs;
  RegisterPrefs(&prefs);
  RegisterAcceptLanguagePrefs(&prefs);

  prefs.SetString(kAcceptLanguagesPref, kAcceptLanguagesJapanese);
  prefs.SetBoolean(prefs::kProfileProxyEnabled, true);
  OfflineGeoIpDatabase::GetInstance()->SetDatabaseDirectory(
      GetGeoIpTestDataDirectory());
  SetProfileProxyPrefs(&prefs, prefs::kProfileProxySchemeHttp, "2001:218::1",
                       8080);
  SyncProfileProxyLanguage(prefs);

  prefs.SetBoolean(prefs::kProfileProxyEnabled, false);
  SyncProfileProxyLanguage(prefs);

  EXPECT_EQ(kAcceptLanguagesJapanese, prefs.GetString(kAcceptLanguagesPref));
  EXPECT_FALSE(prefs.GetBoolean(prefs::kProfileProxyHasSavedAcceptLanguages));
  EXPECT_TRUE(
      prefs.GetString(prefs::kProfileProxySavedAcceptLanguages).empty());
  EXPECT_TRUE(
      prefs.GetString(prefs::kProfileProxyDerivedAcceptLanguages).empty());
  EXPECT_FALSE(GetProfileProxyGeoForPrefs(prefs).has_value());
}

TEST(ProfileProxyConfigTest, UnknownProxyGeoDoesNotOverwriteAcceptLanguages) {
  TestingPrefServiceSimple prefs;
  RegisterPrefs(&prefs);
  RegisterAcceptLanguagePrefs(&prefs);

  prefs.SetString(kAcceptLanguagesPref, kAcceptLanguagesJapanese);
  prefs.SetBoolean(prefs::kProfileProxyEnabled, true);
  SetProfileProxyPrefs(&prefs, prefs::kProfileProxySchemeHttp, "proxy.example",
                       8080);

  SyncProfileProxyLanguage(prefs);

  EXPECT_EQ(kAcceptLanguagesJapanese, prefs.GetString(kAcceptLanguagesPref));
  EXPECT_FALSE(prefs.GetBoolean(prefs::kProfileProxyHasSavedAcceptLanguages));
  EXPECT_TRUE(
      prefs.GetString(prefs::kProfileProxyDerivedAcceptLanguages).empty());
  EXPECT_FALSE(GetProfileProxyGeoForPrefs(prefs).has_value());
  EXPECT_FALSE(ProfileProxyGeoWarning(prefs).empty());
}

TEST(ProfileProxyConfigTest, ManualGeoFallbackAppliesGeoAndLanguage) {
  TestingPrefServiceSimple prefs;
  RegisterPrefs(&prefs);
  RegisterAcceptLanguagePrefs(&prefs);

  prefs.SetString(kAcceptLanguagesPref, kAcceptLanguagesJapanese);
  prefs.SetBoolean(prefs::kProfileProxyEnabled, true);
  SetProfileProxyPrefs(&prefs, prefs::kProfileProxySchemeHttp, "proxy.example",
                       8080);
  prefs.SetBoolean(prefs::kProfileProxyManualGeoEnabled, true);
  prefs.SetString(prefs::kProfileProxyManualGeoCountryCode, "gb");
  prefs.SetString(prefs::kProfileProxyManualGeoTimezone, "Europe/London");
  prefs.SetDouble(prefs::kProfileProxyManualGeoLatitude, 51.5074);
  prefs.SetDouble(prefs::kProfileProxyManualGeoLongitude, -0.1278);

  SyncProfileProxyLanguage(prefs);

  const auto geo = GetProfileProxyGeoForPrefs(prefs);
  ASSERT_TRUE(geo);
  EXPECT_EQ("GB", geo->country_code);
  EXPECT_EQ("Europe/London", geo->timezone);
  EXPECT_DOUBLE_EQ(51.5074, geo->latitude);
  EXPECT_DOUBLE_EQ(-0.1278, geo->longitude);
  EXPECT_EQ(kAcceptLanguagesBritish, prefs.GetString(kAcceptLanguagesPref));
  EXPECT_EQ(kAcceptLanguagesBritish,
            prefs.GetString(prefs::kProfileProxyDerivedAcceptLanguages));
  EXPECT_TRUE(ProfileProxyGeoWarning(prefs).empty());
}

TEST(ProfileProxyConfigTest, ProxyLanguagesOverridePersonaLanguages) {
  TestingPrefServiceSimple prefs;
  RegisterPrefs(&prefs);
  RegisterAcceptLanguagePrefs(&prefs);

  prefs.SetBoolean(prefs::kProfileProxyEnabled, true);
  OfflineGeoIpDatabase::GetInstance()->SetDatabaseDirectory(
      GetGeoIpTestDataDirectory());
  SetProfileProxyPrefs(&prefs, prefs::kProfileProxySchemeHttp, "2001:218::1",
                       8080);
  SyncProfileProxyLanguage(prefs);

  const std::vector<std::string> persona_languages = {"en-US", "en"};
  const std::vector<std::string> expected_languages = {"ja-JP", "ja"};
  EXPECT_EQ(expected_languages,
            GetProfileProxyLanguagesForPrefs(prefs, persona_languages));
}

}  // namespace fingerprint_browser
