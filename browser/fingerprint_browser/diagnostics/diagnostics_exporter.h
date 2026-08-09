/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_FINGERPRINT_BROWSER_DIAGNOSTICS_DIAGNOSTICS_EXPORTER_H_
#define BRAVE_BROWSER_FINGERPRINT_BROWSER_DIAGNOSTICS_DIAGNOSTICS_EXPORTER_H_

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/time/time.h"
#include "base/values.h"

namespace fingerprint_browser::diagnostics {

enum class ExportScope {
  kLatestIncident,
  kLastSevenDays,
};

struct CrashReportDescriptor {
  std::string local_id;
  base::FilePath file_path;
  base::Time capture_time;
  uint64_t size = 0;
};

struct CrashReportSelection {
  std::vector<CrashReportDescriptor> selected;
  std::vector<std::string> omitted_ids;
  uint64_t selected_bytes = 0;
};

struct DiagnosticsBundleRequest {
  ExportScope scope = ExportScope::kLatestIncident;
  base::Time now;
  base::FilePath target_path;
  base::FilePath crashpad_database_path;
  std::vector<CrashReportDescriptor> crash_reports;
  std::vector<base::FilePath> native_reports;
  std::vector<base::FilePath> event_logs;
  std::vector<base::FilePath> debug_logs;
  std::map<std::string, base::DictValue> state_files;
  std::vector<std::string> forbidden_text_values;
  base::RepeatingCallback<std::vector<CrashReportDescriptor>()>
      refresh_crash_reports;
  std::string product_name;
  std::string product_version;
  std::string source_revision;
  std::string module_id;
  base::FilePath module_path;
};

struct DiagnosticsExportResult {
  bool success = false;
  base::FilePath archive_path;
  std::string error;
  size_t crash_count = 0;
  std::vector<std::string> omitted_report_ids;
};

CrashReportSelection SelectCrashReports(
    base::span<const CrashReportDescriptor> reports,
    ExportScope scope,
    base::Time now);

bool IsCrashReportPathSafe(const base::FilePath& database_path,
                           const CrashReportDescriptor& report);

std::string HashSensitiveValue(std::string_view salt, std::string_view value);

std::vector<CrashReportDescriptor> GetCrashReportsFromCrashpadDatabase(
    const base::FilePath& database_path);

std::vector<CrashReportDescriptor> GetCrashReportsFromCrashpad();

DiagnosticsExportResult BuildDiagnosticsBundle(
    const DiagnosticsBundleRequest& request);

}  // namespace fingerprint_browser::diagnostics

#endif  // BRAVE_BROWSER_FINGERPRINT_BROWSER_DIAGNOSTICS_DIAGNOSTICS_EXPORTER_H_
