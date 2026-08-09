/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/fingerprint_browser/diagnostics/diagnostics_data_collector.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/profiler/module_cache.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/system/sys_info.h"
#include "base/version_info/version_info.h"
#include "brave/browser/fingerprint_browser/diagnostics/diagnostics_event_journal.h"
#include "brave/browser/fingerprint_browser/fingerprint_proxy_service.h"
#include "brave/browser/fingerprint_browser/fingerprint_proxy_service_factory.h"
#include "brave/browser/fingerprint_browser/persona_service_factory.h"
#include "brave/components/fingerprint_browser/browser/persona.h"
#include "brave/components/fingerprint_browser/browser/pref_names.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/logging_chrome.h"
#include "components/prefs/pref_service.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension.h"

#if BUILDFLAG(IS_MAC)
#include <dlfcn.h>

#include "base/apple/foundation_util.h"
#include "base/base_paths_apple.h"
#endif

namespace fingerprint_browser::diagnostics {
namespace {

base::DictValue CollectBrowserState(bool has_native_reports) {
  base::DictValue state;
  state.Set("product", std::string(version_info::GetProductName()));
  state.Set("version", std::string(version_info::GetVersionNumber()));
  state.Set("sourceRevision", std::string(version_info::GetLastChange()));
  state.Set("officialBuild", version_info::IsOfficialBuild());
  state.Set("os", std::string(version_info::GetOSType()));
  state.Set("osVersion", base::SysInfo::OperatingSystemVersion());
  state.Set("architecture", base::SysInfo::OperatingSystemArchitecture());
#if BUILDFLAG(IS_MAC)
  state.Set("nativeReportSource", "macos_diagnostic_reports");
#elif BUILDFLAG(IS_WIN)
  state.Set("nativeReportSource", "windows_crashpad");
#else
  state.Set("nativeReportSource", "unavailable_on_platform");
#endif
  state.Set("nativeReportsFound", has_native_reports);
  return state;
}

base::DictValue CollectProfileState(Profile* profile,
                                    std::string_view export_salt) {
  base::DictValue state;
  state.Set("profileHash",
            HashSensitiveValue(export_salt, profile->GetPath().AsUTF8Unsafe()));
  state.Set("offTheRecord", profile->IsOffTheRecord());
  state.Set("guest", profile->IsGuestSession());
  state.Set("regular", profile->IsRegularProfile());
  return state;
}

base::DictValue CollectPersonaState(Profile* profile) {
  base::DictValue state;
  const Persona* persona = GetPersonaForProfile(profile);
  state.Set("enabled", persona != nullptr);
  state.Set("valid", persona && IsPersonaValid(*persona));
  state.Set("schemaVersion", persona ? persona->schema_version : 0);
  state.Set("mediaDeviceKinds",
            persona ? static_cast<int>(persona->media_devices.size()) : 0);
  state.Set("pluginCount",
            persona ? static_cast<int>(persona->plugins.size()) : 0);
  state.Set("fontCount", persona ? static_cast<int>(persona->fonts.size()) : 0);
  state.Set("canvasProtected", persona && !persona->canvas_noise_seed.empty());
  state.Set("audioProtected", persona && !persona->audio_noise_seed.empty());
  state.Set("webglProtected", persona && !persona->webgl.renderer.empty());
  state.Set("webgpuProtected", persona && !persona->webgpu.vendor.empty());
  return state;
}

base::DictValue CollectProxyState(Profile* profile,
                                  std::string_view export_salt,
                                  std::vector<std::string>* forbidden_values) {
  base::DictValue state;
  FingerprintProxyService* service =
      FingerprintProxyServiceFactory::GetForProfile(profile);
  if (!service) {
    state.Set("state", "unavailable");
    return state;
  }

  const FingerprintProxyState proxy = service->GetState();
  state.Set("state", proxy.state);
  state.Set("enabled", proxy.enabled);
  state.Set("scheme", proxy.scheme);
  state.Set("port", proxy.port);
  state.Set("hasSavedPassword", proxy.has_saved_password);
  const int error_code =
      profile->GetPrefs()->GetInteger(prefs::kProfileProxyLastErrorCode);
  if (error_code != 0) {
    state.Set("errorCode", error_code);
  }
  if (!proxy.host.empty()) {
    state.Set("hostHash", HashSensitiveValue(export_salt, proxy.host));
    forbidden_values->push_back(proxy.host);
  }
  if (!proxy.username.empty()) {
    forbidden_values->push_back(proxy.username);
  }
  if (!proxy.egress_ip.empty()) {
    state.Set("egressIpHash", HashSensitiveValue(export_salt, proxy.egress_ip));
    forbidden_values->push_back(proxy.egress_ip);
  }
  state.Set("geoProvider", proxy.geo_provider);
  if (!proxy.last_verified.is_null()) {
    state.Set("lastVerifiedMs",
              base::NumberToString(
                  proxy.last_verified.InMillisecondsSinceUnixEpoch()));
  }
  if (proxy.geo) {
    state.Set("country", proxy.geo->country_code);
    state.Set("timezone", proxy.geo->timezone);
    if (!proxy.geo->accept_languages.empty()) {
      state.Set("language", proxy.geo->accept_languages.front());
    }
    state.Set("webrtcPolicy", "disable_non_proxied_udp");
  }
  return state;
}

base::DictValue CollectExtensionState(Profile* profile) {
  base::DictValue state;
  base::ListValue extensions;
  extensions::ExtensionRegistry* registry =
      extensions::ExtensionRegistry::Get(profile);
  if (!registry) {
    state.Set("items", std::move(extensions));
    return state;
  }

  const extensions::ExtensionSet installed =
      registry->GenerateInstalledExtensionsSet();
  for (const auto& extension : installed) {
    base::DictValue item;
    item.Set("id", extension->id());
    item.Set("name", extension->name());
    item.Set("version", extension->version().GetString());
    item.Set("enabled",
             registry->enabled_extensions().Contains(extension->id()));
    item.Set("source", static_cast<int>(extension->location()));
    item.Set("manifestVersion", extension->manifest_version());
    extensions.Append(std::move(item));
  }
  state.Set("count", static_cast<int>(installed.size()));
  state.Set("items", std::move(extensions));
  return state;
}

std::vector<base::FilePath> CollectNativeReports(
    ExportScope scope,
    base::Time now,
    const std::vector<CrashReportDescriptor>& crash_reports) {
#if BUILDFLAG(IS_MAC)
  const base::FilePath directory =
      base::apple::GetUserLibraryPath().AppendASCII("Logs").AppendASCII(
          "DiagnosticReports");
  base::FilePath normalized_directory;
  if (!base::NormalizeFilePath(directory, &normalized_directory)) {
    return {};
  }
  const std::string process_name =
      base::ToLowerASCII(base::CommandLine::ForCurrentProcess()
                             ->GetProgram()
                             .BaseName()
                             .AsUTF8Unsafe());
  if (process_name.empty()) {
    return {};
  }
  struct NativeReport {
    base::FilePath path;
    base::Time modified;
  };
  std::vector<NativeReport> candidates;
  base::FileEnumerator enumerator(directory, false, base::FileEnumerator::FILES,
                                  FILE_PATH_LITERAL("*.ips"));
  for (base::FilePath path = enumerator.Next(); !path.empty();
       path = enumerator.Next()) {
    base::FilePath normalized_path;
    if (!base::NormalizeFilePath(path, &normalized_path) ||
        normalized_path.DirName() != normalized_directory ||
        normalized_path.Extension() != FILE_PATH_LITERAL(".ips")) {
      continue;
    }
    const std::string name =
        base::ToLowerASCII(normalized_path.BaseName().AsUTF8Unsafe());
    if (!base::StartsWith(name, process_name)) {
      continue;
    }
    base::File::Info info;
    if (base::GetFileInfo(normalized_path, &info) && !info.is_directory) {
      candidates.push_back({normalized_path, info.last_modified});
    }
  }
  std::ranges::sort(candidates,
                    [](const NativeReport& left, const NativeReport& right) {
                      return left.modified > right.modified;
                    });

  base::Time anchor;
  for (const auto& report : crash_reports) {
    if (anchor.is_null() || report.capture_time > anchor) {
      anchor = report.capture_time;
    }
  }
  if (anchor.is_null() && !candidates.empty()) {
    anchor = candidates.front().modified;
  }

  const size_t maximum = scope == ExportScope::kLatestIncident ? 10u : 20u;
  std::vector<base::FilePath> reports;
  for (const auto& candidate : candidates) {
    const bool in_range =
        scope == ExportScope::kLatestIncident
            ? !anchor.is_null() &&
                  candidate.modified >= anchor - base::Minutes(5) &&
                  candidate.modified <= anchor + base::Minutes(5)
            : candidate.modified >= now - base::Days(7) &&
                  candidate.modified <= now;
    if (in_range && reports.size() < maximum) {
      reports.push_back(candidate.path);
    }
  }
  return reports;
#else
  return {};
#endif
}

std::vector<base::FilePath> CollectDebugLogs() {
  const base::FilePath path =
      logging::GetLogFileName(*base::CommandLine::ForCurrentProcess());
  return !path.empty() && base::PathExists(path)
             ? std::vector<base::FilePath>{path}
             : std::vector<base::FilePath>{};
}

void CollectModuleIdentity(CollectedDiagnosticsData* data) {
  base::ModuleCache cache;
  const auto* module = cache.GetModuleForAddress(
      reinterpret_cast<uintptr_t>(&CompleteDiagnosticsData));
  if (module) {
    data->module_id = module->GetId();
  }
#if BUILDFLAG(IS_MAC)
  Dl_info module_info = {};
  if (dladdr(reinterpret_cast<const void*>(&CompleteDiagnosticsData),
             &module_info) != 0 &&
      module_info.dli_fname) {
    data->module_path = base::FilePath::FromUTF8Unsafe(module_info.dli_fname);
  }
#else
  base::PathService::Get(base::FILE_MODULE, &data->module_path);
#endif
  if (data->module_path.empty() && module) {
    data->module_path = module->GetDebugBasename();
  }
}

}  // namespace

CollectedDiagnosticsData CollectDiagnosticsProfileData(
    Profile* profile,
    std::string_view export_salt) {
  CollectedDiagnosticsData data;
  data.product_name = std::string(version_info::GetProductName());
  data.product_version = std::string(version_info::GetVersionNumber());
  data.source_revision = std::string(version_info::GetLastChange());
  data.state_files.emplace("profiles",
                           CollectProfileState(profile, export_salt));
  data.state_files.emplace("fingerprint", CollectPersonaState(profile));
  data.state_files.emplace(
      "proxy",
      CollectProxyState(profile, export_salt, &data.forbidden_text_values));
  data.state_files.emplace("extensions", CollectExtensionState(profile));
  return data;
}

CollectedDiagnosticsData CompleteDiagnosticsData(
    CollectedDiagnosticsData data,
    const base::FilePath& user_data_dir,
    ExportScope scope,
    base::Time now,
    const std::vector<CrashReportDescriptor>& crash_reports) {
  data.native_reports = CollectNativeReports(scope, now, crash_reports);
  data.state_files.emplace("browser",
                           CollectBrowserState(!data.native_reports.empty() ||
                                               !crash_reports.empty()));
  data.event_logs = GetDiagnosticsEventLogs(user_data_dir, now);
  data.debug_logs = CollectDebugLogs();
  CollectModuleIdentity(&data);
  return data;
}

}  // namespace fingerprint_browser::diagnostics
