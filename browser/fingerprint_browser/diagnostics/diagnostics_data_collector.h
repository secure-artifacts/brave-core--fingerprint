/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_FINGERPRINT_BROWSER_DIAGNOSTICS_DIAGNOSTICS_DATA_COLLECTOR_H_
#define BRAVE_BROWSER_FINGERPRINT_BROWSER_DIAGNOSTICS_DIAGNOSTICS_DATA_COLLECTOR_H_

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/time/time.h"
#include "base/values.h"
#include "brave/browser/fingerprint_browser/diagnostics/diagnostics_exporter.h"

class Profile;

namespace fingerprint_browser::diagnostics {

struct CollectedDiagnosticsData {
  std::map<std::string, base::DictValue> state_files;
  std::vector<base::FilePath> native_reports;
  std::vector<base::FilePath> event_logs;
  std::vector<base::FilePath> debug_logs;
  std::vector<std::string> forbidden_text_values;
  std::string product_name;
  std::string product_version;
  std::string source_revision;
  std::string module_id;
  base::FilePath module_path;
};

CollectedDiagnosticsData CollectDiagnosticsProfileData(
    Profile* profile,
    std::string_view export_salt);

CollectedDiagnosticsData CompleteDiagnosticsData(
    CollectedDiagnosticsData data,
    const base::FilePath& user_data_dir,
    ExportScope scope,
    base::Time now,
    const std::vector<CrashReportDescriptor>& crash_reports);

}  // namespace fingerprint_browser::diagnostics

#endif  // BRAVE_BROWSER_FINGERPRINT_BROWSER_DIAGNOSTICS_DIAGNOSTICS_DATA_COLLECTOR_H_
