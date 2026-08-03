/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/fingerprint_browser/diagnostics/diagnostics_exporter.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "components/crash/core/app/crashpad.h"
#include "components/feedback/redaction_tool/redaction_tool.h"
#include "crypto/hash.h"
#include "crypto/sha2.h"
#include "third_party/crashpad/crashpad/client/crash_report_database.h"
#include "third_party/crashpad/crashpad/util/misc/uuid.h"
#include "third_party/zlib/google/zip.h"

namespace fingerprint_browser::diagnostics {
namespace {

constexpr size_t kLatestIncidentMaxReports = 10;
constexpr uint64_t kLatestIncidentMaxBytes = 100 * 1024 * 1024;
constexpr size_t kLastSevenDaysMaxReports = 20;
constexpr uint64_t kLastSevenDaysMaxBytes = 250 * 1024 * 1024;
constexpr base::TimeDelta kIncidentWindow = base::Minutes(5);
constexpr base::TimeDelta kSevenDays = base::Days(7);

std::string ExportScopeToString(ExportScope scope) {
  return scope == ExportScope::kLatestIncident ? "latest_incident"
                                               : "last_7_days";
}

std::string Sha256File(const base::FilePath& path) {
  base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!file.IsValid()) {
    return {};
  }
  std::array<uint8_t, crypto::hash::kSha256Size> hash;
  if (!crypto::hash::HashFile(crypto::hash::kSha256, &file, hash)) {
    return {};
  }
  return base::HexEncodeLower(hash);
}

bool WriteJson(const base::FilePath& path, const base::DictValue& value) {
  std::string output;
  if (!base::JSONWriter::WriteWithOptions(
          value, base::JSONWriter::OPTIONS_PRETTY_PRINT, &output)) {
    return false;
  }
  return base::WriteFile(path, output);
}

bool ContainsForbiddenText(const base::FilePath& root,
                           base::span<const std::string> forbidden_values) {
  base::FileEnumerator files(root, true, base::FileEnumerator::FILES);
  for (base::FilePath path = files.Next(); !path.empty(); path = files.Next()) {
    const auto extension = path.Extension();
    if (extension != FILE_PATH_LITERAL(".json") &&
        extension != FILE_PATH_LITERAL(".jsonl") &&
        extension != FILE_PATH_LITERAL(".log") &&
        extension != FILE_PATH_LITERAL(".txt")) {
      continue;
    }
    std::string contents;
    if (!base::ReadFileToString(path, &contents)) {
      return true;
    }
    for (const auto& forbidden : forbidden_values) {
      if (!forbidden.empty() && contents.contains(forbidden)) {
        return true;
      }
    }
  }
  return false;
}

bool CopyPayloadFile(const base::FilePath& source,
                     const base::FilePath& destination) {
  return base::PathExists(source) && base::CopyFile(source, destination);
}

bool IsMinidumpFile(const base::FilePath& path) {
  base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!file.IsValid()) {
    return false;
  }
  std::array<uint8_t, 4> signature;
  return file.ReadAtCurrentPos(signature) == signature.size() &&
         signature == std::array<uint8_t, 4>{'M', 'D', 'M', 'P'};
}

bool CopyCrashReportFile(const base::FilePath& database_path,
                         const CrashReportDescriptor& report,
                         const base::FilePath& destination) {
  base::FilePath normalized_database;
  base::FilePath normalized_report;
  return IsCrashReportPathSafe(database_path, report) &&
         base::NormalizeFilePath(database_path, &normalized_database) &&
         base::NormalizeFilePath(report.file_path, &normalized_report) &&
         normalized_database.IsParent(normalized_report) &&
         IsMinidumpFile(normalized_report) &&
         base::CopyFile(normalized_report, destination);
}

bool CopyRedactedLog(const base::FilePath& source,
                     const base::FilePath& destination) {
  std::string contents;
  if (!base::ReadFileToStringWithMaxSize(source, &contents, 5 * 1024 * 1024)) {
    return false;
  }
  redaction::RedactionTool redactor;
  return base::WriteFile(destination, redactor.Redact(contents));
}

std::vector<CrashReportDescriptor> ConvertCrashpadReports(
    const std::vector<crashpad::CrashReportDatabase::Report>& reports) {
  std::vector<CrashReportDescriptor> converted;
  for (const auto& report : reports) {
    const std::optional<int64_t> file_size =
        base::GetFileSize(report.file_path);
    if (!file_size || *file_size < 0) {
      continue;
    }
    converted.push_back(
        {.local_id = report.uuid.ToString(),
         .file_path = report.file_path,
         .capture_time = base::Time::FromTimeT(report.creation_time),
         .size = static_cast<uint64_t>(*file_size)});
  }
  return converted;
}

std::optional<std::map<std::string, std::string>> BuildFileHashInventory(
    const base::FilePath& root) {
  std::map<std::string, std::string> inventory;
  base::FileEnumerator files(root, true, base::FileEnumerator::FILES);
  for (base::FilePath path = files.Next(); !path.empty(); path = files.Next()) {
    base::FilePath relative;
    if (!root.AppendRelativePath(path, &relative)) {
      return std::nullopt;
    }
    const std::string hash = Sha256File(path);
    if (hash.empty()) {
      return std::nullopt;
    }
    inventory.emplace(relative.AsUTF8Unsafe(), hash);
  }
  return inventory;
}

bool VerifyArchive(const base::FilePath& archive,
                   const base::FilePath& payload,
                   int64_t maximum_bytes) {
  const std::optional<int64_t> archive_size = base::GetFileSize(archive);
  if (!archive_size || *archive_size <= 0 || *archive_size > maximum_bytes) {
    return false;
  }

  base::ScopedTempDir extracted;
  if (!extracted.CreateUniqueTempDir() ||
      !zip::Unzip(archive, extracted.GetPath())) {
    return false;
  }
  return BuildFileHashInventory(payload) ==
         BuildFileHashInventory(extracted.GetPath());
}

}  // namespace

CrashReportSelection SelectCrashReports(
    base::span<const CrashReportDescriptor> reports,
    ExportScope scope,
    base::Time now) {
  std::vector<CrashReportDescriptor> sorted(reports.begin(), reports.end());
  std::ranges::sort(sorted, [](const auto& left, const auto& right) {
    return left.capture_time > right.capture_time;
  });

  CrashReportSelection selection;
  if (sorted.empty()) {
    return selection;
  }

  const size_t max_reports = scope == ExportScope::kLatestIncident
                                 ? kLatestIncidentMaxReports
                                 : kLastSevenDaysMaxReports;
  const uint64_t max_bytes = scope == ExportScope::kLatestIncident
                                 ? kLatestIncidentMaxBytes
                                 : kLastSevenDaysMaxBytes;
  const base::Time earliest =
      scope == ExportScope::kLatestIncident
          ? sorted.front().capture_time - kIncidentWindow
          : now - kSevenDays;
  std::set<std::string> seen_report_ids;

  for (const auto& report : sorted) {
    if (!seen_report_ids.insert(report.local_id).second) {
      continue;
    }
    const bool in_time_range =
        report.capture_time >= earliest && report.capture_time <= now;
    const bool within_count = selection.selected.size() < max_reports;
    const bool within_size =
        report.size <= max_bytes - selection.selected_bytes;
    if (!in_time_range || !within_count || !within_size) {
      selection.omitted_ids.push_back(report.local_id);
      continue;
    }
    selection.selected.push_back(report);
    selection.selected_bytes += report.size;
  }
  return selection;
}

bool IsCrashReportPathSafe(const base::FilePath& database_path,
                           const CrashReportDescriptor& report) {
  crashpad::UUID uuid;
  if (!database_path.IsParent(report.file_path) ||
      report.file_path.Extension() != FILE_PATH_LITERAL(".dmp") ||
      !uuid.InitializeFromString(report.local_id)) {
    return false;
  }
  return report.file_path.BaseName().RemoveExtension().AsUTF8Unsafe() ==
         report.local_id;
}

std::string HashSensitiveValue(std::string_view salt, std::string_view value) {
  std::string input(salt);
  input.push_back('\0');
  input.append(value);
  return base::HexEncodeLower(crypto::SHA256HashString(input));
}

std::vector<CrashReportDescriptor> GetCrashReportsFromCrashpad() {
  auto* database = crash_reporter::internal::GetCrashReportDatabase();
  if (!database) {
    return {};
  }

  std::vector<crashpad::CrashReportDatabase::Report> pending;
  std::vector<crashpad::CrashReportDatabase::Report> completed;
  if (database->GetPendingReports(&pending) !=
          crashpad::CrashReportDatabase::kNoError ||
      database->GetCompletedReports(&completed) !=
          crashpad::CrashReportDatabase::kNoError) {
    return {};
  }

  std::vector<CrashReportDescriptor> reports = ConvertCrashpadReports(pending);
  auto completed_reports = ConvertCrashpadReports(completed);
  reports.insert(reports.end(),
                 std::make_move_iterator(completed_reports.begin()),
                 std::make_move_iterator(completed_reports.end()));
  return reports;
}

DiagnosticsExportResult BuildDiagnosticsBundle(
    const DiagnosticsBundleRequest& request) {
  DiagnosticsExportResult result;
  if (request.target_path.empty()) {
    result.error = "missing_output_path";
    return result;
  }

  base::ScopedTempDir payload;
  if (!payload.CreateUniqueTempDir()) {
    result.error = "create_temporary_directory_failed";
    return result;
  }

  const base::FilePath crashes_dir = payload.GetPath().AppendASCII("crashes");
  const base::FilePath native_dir = payload.GetPath().AppendASCII("native");
  const base::FilePath logs_dir = payload.GetPath().AppendASCII("logs");
  const base::FilePath state_dir = payload.GetPath().AppendASCII("state");
  if (!base::CreateDirectory(crashes_dir) ||
      !base::CreateDirectory(native_dir) || !base::CreateDirectory(logs_dir) ||
      !base::CreateDirectory(state_dir)) {
    result.error = "create_payload_directories_failed";
    return result;
  }

  CrashReportSelection selection =
      SelectCrashReports(request.crash_reports, request.scope, request.now);
  result.omitted_report_ids = selection.omitted_ids;
  for (const auto& report : selection.selected) {
    if (!IsCrashReportPathSafe(request.crashpad_database_path, report)) {
      result.omitted_report_ids.push_back(report.local_id);
      continue;
    }
    const base::FilePath destination =
        crashes_dir.AppendASCII(report.local_id + ".dmp");
    bool copied = CopyCrashReportFile(request.crashpad_database_path, report,
                                      destination);
    if (!copied && request.refresh_crash_reports) {
      const auto refreshed = request.refresh_crash_reports.Run();
      const auto current = std::ranges::find(refreshed, report.local_id,
                                             &CrashReportDescriptor::local_id);
      if (current != refreshed.end() &&
          IsCrashReportPathSafe(request.crashpad_database_path, *current)) {
        copied = CopyCrashReportFile(request.crashpad_database_path, *current,
                                     destination);
      }
    }
    if (!copied) {
      result.omitted_report_ids.push_back(report.local_id);
      continue;
    }
    ++result.crash_count;
  }

  for (const auto& report : request.native_reports) {
    CopyPayloadFile(report, native_dir.Append(report.BaseName()));
  }
  for (const auto& log : request.event_logs) {
    if (!CopyPayloadFile(log, logs_dir.Append(log.BaseName()))) {
      result.error = "copy_event_log_failed";
      return result;
    }
  }
  for (const auto& log : request.debug_logs) {
    if (!CopyRedactedLog(log,
                         logs_dir.AppendASCII("chrome-debug-redacted.log"))) {
      result.error = "redact_debug_log_failed";
      return result;
    }
  }
  for (const auto& [name, state] : request.state_files) {
    if (!WriteJson(state_dir.AppendASCII(name + ".json"), state)) {
      result.error = "write_state_failed";
      return result;
    }
  }

  constexpr std::string_view kReadme =
      "此诊断包在本地创建，不会自动上传。崩溃转储可能包含进程内存片段，"
      "请仅发送给您信任的技术支持人员。代理凭证、Cookie、历史记录和原始"
      "用户配置文件设置不会写入诊断包。\n";
  if (!base::WriteFile(payload.GetPath().AppendASCII("README.txt"), kReadme)) {
    result.error = "write_readme_failed";
    return result;
  }

  if (ContainsForbiddenText(payload.GetPath(), request.forbidden_text_values)) {
    result.error = "forbidden_text_detected";
    return result;
  }

  const int64_t max_archive_bytes =
      request.scope == ExportScope::kLatestIncident
          ? static_cast<int64_t>(kLatestIncidentMaxBytes)
          : static_cast<int64_t>(kLastSevenDaysMaxBytes);
  if (base::ComputeDirectorySize(payload.GetPath()) > max_archive_bytes) {
    result.error = "archive_size_limit_exceeded";
    return result;
  }

  base::DictValue manifest;
  manifest.Set("schemaVersion", 1);
  manifest.Set("scope", ExportScopeToString(request.scope));
  manifest.Set(
      "generatedAtMs",
      base::NumberToString(request.now.InMillisecondsSinceUnixEpoch()));
  manifest.Set("product", request.product_name);
  manifest.Set("version", request.product_version);
  manifest.Set("sourceRevision", request.source_revision);
  base::DictValue module;
  module.Set("name", request.module_path.BaseName().AsUTF8Unsafe());
  module.Set("id", request.module_id);
  if (!request.module_path.empty() && base::PathExists(request.module_path)) {
    const std::string module_sha256 = Sha256File(request.module_path);
    if (module_sha256.empty()) {
      result.error = "hash_module_failed";
      return result;
    }
    module.Set("sha256", module_sha256);
  }
  manifest.Set("module", std::move(module));
  manifest.Set("crashCount", static_cast<int>(result.crash_count));
  base::ListValue omissions;
  for (const auto& id : result.omitted_report_ids) {
    omissions.Append(id);
  }
  manifest.Set("omittedCrashReports", std::move(omissions));

  base::ListValue payload_files;
  base::FileEnumerator files(payload.GetPath(), true,
                             base::FileEnumerator::FILES);
  for (base::FilePath path = files.Next(); !path.empty(); path = files.Next()) {
    base::FilePath relative;
    if (!payload.GetPath().AppendRelativePath(path, &relative)) {
      result.error = "invalid_payload_path";
      return result;
    }
    const std::optional<int64_t> size = base::GetFileSize(path);
    if (!size) {
      result.error = "read_payload_size_failed";
      return result;
    }
    base::DictValue file;
    file.Set("path", relative.AsUTF8Unsafe());
    file.Set("sizeBytes", base::NumberToString(*size));
    const std::string sha256 = Sha256File(path);
    if (sha256.empty()) {
      result.error = "hash_payload_failed";
      return result;
    }
    file.Set("sha256", sha256);
    payload_files.Append(std::move(file));
  }
  manifest.Set("files", std::move(payload_files));

  const base::FilePath manifest_path =
      payload.GetPath().AppendASCII("manifest.json");
  if (!WriteJson(manifest_path, manifest)) {
    result.error = "write_manifest_failed";
    return result;
  }

  std::string checksums;
  base::FileEnumerator checksum_files(payload.GetPath(), true,
                                      base::FileEnumerator::FILES);
  for (base::FilePath path = checksum_files.Next(); !path.empty();
       path = checksum_files.Next()) {
    base::FilePath relative;
    if (!payload.GetPath().AppendRelativePath(path, &relative)) {
      result.error = "invalid_checksum_path";
      return result;
    }
    const std::string sha256 = Sha256File(path);
    if (sha256.empty()) {
      result.error = "hash_checksum_file_failed";
      return result;
    }
    checksums.append(sha256);
    checksums.append("  ");
    checksums.append(relative.AsUTF8Unsafe());
    checksums.push_back('\n');
  }
  if (!base::WriteFile(payload.GetPath().AppendASCII("checksums.sha256"),
                       checksums)) {
    result.error = "write_checksums_failed";
    return result;
  }

  base::FilePath temporary_archive;
  if (!base::CreateTemporaryFileInDir(request.target_path.DirName(),
                                      &temporary_archive) ||
      !base::DeleteFile(temporary_archive) ||
      !zip::Zip(payload.GetPath(), temporary_archive, true)) {
    base::DeleteFile(temporary_archive);
    result.error = "create_archive_failed";
    return result;
  }

  if (!VerifyArchive(temporary_archive, payload.GetPath(), max_archive_bytes)) {
    base::DeleteFile(temporary_archive);
    result.error = "verify_archive_failed";
    return result;
  }

  base::File::Error file_error;
  if (!base::ReplaceFile(temporary_archive, request.target_path, &file_error)) {
    base::DeleteFile(temporary_archive);
    result.error = base::StringPrintf("place_archive_failed:%d", file_error);
    return result;
  }

  result.success = true;
  result.archive_path = request.target_path;
  return result;
}

}  // namespace fingerprint_browser::diagnostics
