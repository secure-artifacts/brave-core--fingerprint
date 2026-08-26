/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_FINGERPRINT_BROWSER_DIAGNOSTICS_DIAGNOSTICS_EVENT_JOURNAL_H_
#define BRAVE_BROWSER_FINGERPRINT_BROWSER_DIAGNOSTICS_DIAGNOSTICS_EVENT_JOURNAL_H_

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/memory/scoped_refptr.h"
#include "base/time/time.h"

namespace base {
class SequencedTaskRunner;
}

namespace fingerprint_browser::diagnostics {

enum class DiagnosticsEventType {
  kBrowserStarted,
  kBrowserStopping,
  kProfileLoaded,
  kPersonaLoaded,
  kProxyStateChanged,
  kGeoStateChanged,
  kExtensionInstalled,
  kExtensionUninstalled,
  kDiagnosticsExported,
  kError,
};

struct DiagnosticsEventFields {
  std::string status;
  std::string profile_type;
  std::string extension_id;
  std::string extension_version;
  std::string proxy_scheme;
  std::string proxy_status_code;
  std::string country;
  std::string timezone;
  std::string language;
  int error_code = 0;
  int persona_schema = 0;
};

bool RecordDiagnosticsEvent(const base::FilePath& user_data_dir,
                            DiagnosticsEventType type,
                            const DiagnosticsEventFields& fields = {},
                            base::Time now = base::Time::Now());

void RotateDiagnosticsEventLogs(const base::FilePath& user_data_dir,
                                base::Time now = base::Time::Now());

std::vector<base::FilePath> GetDiagnosticsEventLogs(
    const base::FilePath& user_data_dir,
    base::Time now = base::Time::Now());

scoped_refptr<base::SequencedTaskRunner> GetDiagnosticsEventTaskRunner();

}  // namespace fingerprint_browser::diagnostics

#endif  // BRAVE_BROWSER_FINGERPRINT_BROWSER_DIAGNOSTICS_DIAGNOSTICS_EVENT_JOURNAL_H_
