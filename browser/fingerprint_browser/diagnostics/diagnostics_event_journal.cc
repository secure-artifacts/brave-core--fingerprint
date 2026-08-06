/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/fingerprint_browser/diagnostics/diagnostics_event_journal.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/location.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/lazy_thread_pool_task_runner.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"

namespace fingerprint_browser::diagnostics {
namespace {

constexpr base::TimeDelta kRetention = base::Days(7);
constexpr int64_t kMaximumJournalFileBytes = 5 * 1024 * 1024;
constexpr int64_t kMaximumJournalBytes = 20 * 1024 * 1024;
constexpr size_t kMaximumFieldLength = 128;

base::LazyThreadPoolSequencedTaskRunner g_journal_task_runner =
    LAZY_THREAD_POOL_SEQUENCED_TASK_RUNNER_INITIALIZER(base::TaskTraits(
        base::MayBlock(), base::TaskPriority::USER_VISIBLE,
        base::TaskShutdownBehavior::BLOCK_SHUTDOWN));

base::Lock& JournalLock() {
  static base::NoDestructor<base::Lock> lock;
  return *lock;
}

scoped_refptr<base::SequencedTaskRunner> JournalTaskRunner() {
  return g_journal_task_runner.Get();
}

base::FilePath JournalDirectory(const base::FilePath& user_data_dir) {
  return user_data_dir.AppendASCII("FingerprintDiagnostics");
}

std::string EventTypeToString(DiagnosticsEventType type) {
  switch (type) {
    case DiagnosticsEventType::kBrowserStarted:
      return "browser_started";
    case DiagnosticsEventType::kBrowserStopping:
      return "browser_stopping";
    case DiagnosticsEventType::kProfileLoaded:
      return "profile_loaded";
    case DiagnosticsEventType::kPersonaLoaded:
      return "persona_loaded";
    case DiagnosticsEventType::kProxyStateChanged:
      return "proxy_state_changed";
    case DiagnosticsEventType::kGeoStateChanged:
      return "geo_state_changed";
    case DiagnosticsEventType::kExtensionInstalled:
      return "extension_installed";
    case DiagnosticsEventType::kExtensionUninstalled:
      return "extension_uninstalled";
    case DiagnosticsEventType::kDiagnosticsExported:
      return "diagnostics_exported";
    case DiagnosticsEventType::kError:
      return "error";
  }
}

std::string SafeField(std::string_view value) {
  std::string sanitized(value.substr(0, kMaximumFieldLength));
  base::ReplaceChars(sanitized, "\r\n\t", " ", &sanitized);
  return sanitized;
}

std::string JournalStem(base::Time now) {
  base::Time::Exploded exploded;
  now.UTCExplode(&exploded);
  return base::StringPrintf("fingerprint-events-%04d%02d%02d", exploded.year,
                            exploded.month, exploded.day_of_month);
}

base::FilePath JournalPathForAppend(const base::FilePath& user_data_dir,
                                    base::Time now,
                                    size_t append_bytes) {
  const std::string stem = JournalStem(now);
  for (int index = 0; index < 1000; ++index) {
    const std::string name =
        index == 0 ? stem + ".jsonl"
                   : base::StringPrintf("%s-%03d.jsonl", stem.c_str(), index);
    const base::FilePath candidate =
        JournalDirectory(user_data_dir).AppendASCII(name);
    const std::optional<int64_t> current_size = base::GetFileSize(candidate);
    if (!current_size ||
        *current_size <=
            kMaximumJournalFileBytes - static_cast<int64_t>(append_bytes)) {
      return candidate;
    }
  }
  return {};
}

struct JournalFile {
  base::FilePath path;
  base::Time modified;
  int64_t size = 0;
};

std::vector<JournalFile> EnumerateJournalFiles(
    const base::FilePath& user_data_dir) {
  std::vector<JournalFile> files;
  base::FileEnumerator enumerator(
      JournalDirectory(user_data_dir), false, base::FileEnumerator::FILES,
      FILE_PATH_LITERAL("fingerprint-events-*.jsonl"));
  for (base::FilePath path = enumerator.Next(); !path.empty();
       path = enumerator.Next()) {
    base::File::Info info;
    if (base::GetFileInfo(path, &info) && !info.is_directory) {
      files.push_back({path, info.last_modified, info.size});
    }
  }
  return files;
}

void RotateLocked(const base::FilePath& user_data_dir, base::Time now) {
  std::vector<JournalFile> files = EnumerateJournalFiles(user_data_dir);
  const base::Time earliest = now - kRetention;
  for (const auto& file : files) {
    if (file.modified < earliest) {
      base::DeleteFile(file.path);
    }
  }

  files = EnumerateJournalFiles(user_data_dir);
  std::ranges::sort(files,
                    [](const JournalFile& left, const JournalFile& right) {
                      return left.modified > right.modified;
                    });
  int64_t retained_bytes = 0;
  for (const auto& file : files) {
    if (file.size < 0 || file.size > kMaximumJournalBytes - retained_bytes) {
      base::DeleteFile(file.path);
      continue;
    }
    retained_bytes += file.size;
  }
}

bool RecordDiagnosticsEventBlocking(const base::FilePath& user_data_dir,
                                    DiagnosticsEventType type,
                                    const DiagnosticsEventFields& fields,
                                    base::Time now) {
  base::AutoLock lock(JournalLock());
  RotateLocked(user_data_dir, now);
  if (!base::CreateDirectory(JournalDirectory(user_data_dir))) {
    return false;
  }

  base::DictValue event;
  event.Set("schemaVersion", 1);
  event.Set("timeMs", base::NumberToString(now.InMillisecondsSinceUnixEpoch()));
  event.Set("event", EventTypeToString(type));
  if (!fields.status.empty()) {
    event.Set("status", SafeField(fields.status));
  }
  if (!fields.profile_type.empty()) {
    event.Set("profileType", SafeField(fields.profile_type));
  }
  if (!fields.extension_id.empty()) {
    event.Set("extensionId", SafeField(fields.extension_id));
  }
  if (!fields.extension_version.empty()) {
    event.Set("extensionVersion", SafeField(fields.extension_version));
  }
  if (!fields.proxy_scheme.empty()) {
    event.Set("proxyScheme", SafeField(fields.proxy_scheme));
  }
  if (!fields.country.empty()) {
    event.Set("country", SafeField(fields.country));
  }
  if (!fields.timezone.empty()) {
    event.Set("timezone", SafeField(fields.timezone));
  }
  if (!fields.language.empty()) {
    event.Set("language", SafeField(fields.language));
  }
  if (fields.error_code != 0) {
    event.Set("errorCode", fields.error_code);
  }
  if (fields.persona_schema != 0) {
    event.Set("personaSchema", fields.persona_schema);
  }

  std::string json;
  if (!base::JSONWriter::Write(event, &json)) {
    return false;
  }
  json.push_back('\n');

  const base::FilePath journal_path =
      JournalPathForAppend(user_data_dir, now, json.size());
  if (journal_path.empty()) {
    return false;
  }
  base::File file(journal_path,
                  base::File::FLAG_OPEN_ALWAYS | base::File::FLAG_APPEND);
  if (!file.IsValid() ||
      !file.WriteAtCurrentPosAndCheck(base::as_byte_span(base::span(json))) ||
      !file.Flush()) {
    return false;
  }
  RotateLocked(user_data_dir, now);
  return true;
}

void FlushPendingJournalTasks() {
  if (JournalTaskRunner()->RunsTasksInCurrentSequence()) {
    return;
  }
  base::WaitableEvent flushed;
  if (!JournalTaskRunner()->PostTask(
          FROM_HERE, base::BindOnce(&base::WaitableEvent::Signal,
                                    base::Unretained(&flushed)))) {
    return;
  }
  flushed.Wait();
}

}  // namespace

bool RecordDiagnosticsEvent(const base::FilePath& user_data_dir,
                            DiagnosticsEventType type,
                            const DiagnosticsEventFields& fields,
                            base::Time now) {
  return JournalTaskRunner()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::FilePath path, DiagnosticsEventType event_type,
             DiagnosticsEventFields event_fields, base::Time event_time) {
            RecordDiagnosticsEventBlocking(path, event_type, event_fields,
                                           event_time);
          },
          user_data_dir, type, fields, now));
}

void RotateDiagnosticsEventLogs(const base::FilePath& user_data_dir,
                                base::Time now) {
  JournalTaskRunner()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::FilePath path, base::Time rotation_time) {
                       base::AutoLock lock(JournalLock());
                       RotateLocked(path, rotation_time);
                     },
                     user_data_dir, now));
}

std::vector<base::FilePath> GetDiagnosticsEventLogs(
    const base::FilePath& user_data_dir,
    base::Time now) {
  FlushPendingJournalTasks();
  base::AutoLock lock(JournalLock());
  RotateLocked(user_data_dir, now);
  std::vector<JournalFile> files = EnumerateJournalFiles(user_data_dir);
  std::ranges::sort(files,
                    [](const JournalFile& left, const JournalFile& right) {
                      return left.modified < right.modified;
                    });
  std::vector<base::FilePath> paths;
  paths.reserve(files.size());
  for (const auto& file : files) {
    paths.push_back(file.path);
  }
  return paths;
}

scoped_refptr<base::SequencedTaskRunner> GetDiagnosticsEventTaskRunner() {
  return JournalTaskRunner();
}

}  // namespace fingerprint_browser::diagnostics
