/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/fingerprint_browser/diagnostics/diagnostics_exporter.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/crashpad/crashpad/client/crash_report_database.h"
#include "third_party/crashpad/crashpad/util/misc/uuid.h"
#include "third_party/zlib/google/zip_reader.h"

namespace fingerprint_browser::diagnostics {
namespace {

constexpr uint64_t kMiB = 1024 * 1024;
constexpr char kReportId[] = "00112233-4455-6677-8899-aabbccddeeff";

CrashReportDescriptor Report(std::string id,
                             base::Time capture_time,
                             uint64_t size = kMiB) {
  return {.local_id = id,
          .file_path = base::FilePath(FILE_PATH_LITERAL("/db/pending"))
                           .AppendASCII(id + ".dmp"),
          .capture_time = capture_time,
          .size = size};
}

void CreateCrashpadReport(crashpad::CrashReportDatabase* database,
                          std::string_view contents,
                          crashpad::UUID* uuid) {
  std::unique_ptr<crashpad::CrashReportDatabase::NewReport> report;
  ASSERT_EQ(database->PrepareNewCrashReport(&report),
            crashpad::CrashReportDatabase::kNoError);
  ASSERT_TRUE(report->Writer()->Write(contents.data(), contents.size()));
  ASSERT_EQ(database->FinishedWritingCrashReport(std::move(report), uuid),
            crashpad::CrashReportDatabase::kNoError);
}

TEST(DiagnosticsExporterTest, SelectsLatestIncidentNewestFirst) {
  const base::Time now = base::Time::Now();
  const std::vector<CrashReportDescriptor> reports = {
      Report("older", now - base::Minutes(7)),
      Report("latest", now - base::Minutes(1)),
      Report("related", now - base::Minutes(4)),
  };

  CrashReportSelection selection =
      SelectCrashReports(reports, ExportScope::kLatestIncident, now);

  ASSERT_EQ(selection.selected.size(), 2u);
  EXPECT_EQ(selection.selected[0].local_id, "latest");
  EXPECT_EQ(selection.selected[1].local_id, "related");
  EXPECT_EQ(selection.omitted_ids, std::vector<std::string>({"older"}));
}

TEST(DiagnosticsExporterTest, AppliesLatestIncidentSizeLimit) {
  const base::Time now = base::Time::Now();
  const std::vector<CrashReportDescriptor> reports = {
      Report("latest", now, 70 * kMiB),
      Report("too-large", now - base::Minutes(1), 40 * kMiB),
      Report("fits", now - base::Minutes(2), 20 * kMiB),
  };

  CrashReportSelection selection =
      SelectCrashReports(reports, ExportScope::kLatestIncident, now);

  ASSERT_EQ(selection.selected.size(), 2u);
  EXPECT_EQ(selection.selected[0].local_id, "latest");
  EXPECT_EQ(selection.selected[1].local_id, "fits");
  EXPECT_EQ(selection.selected_bytes, 90 * kMiB);
  EXPECT_EQ(selection.omitted_ids, std::vector<std::string>({"too-large"}));
}

TEST(DiagnosticsExporterTest, SevenDayScopeExcludesExpiredReports) {
  const base::Time now = base::Time::Now();
  const std::vector<CrashReportDescriptor> reports = {
      Report("expired", now - base::Days(8)),
      Report("recent", now - base::Days(2)),
  };

  CrashReportSelection selection =
      SelectCrashReports(reports, ExportScope::kLastSevenDays, now);

  ASSERT_EQ(selection.selected.size(), 1u);
  EXPECT_EQ(selection.selected[0].local_id, "recent");
  EXPECT_EQ(selection.omitted_ids, std::vector<std::string>({"expired"}));
}

TEST(DiagnosticsExporterTest, DeduplicatesReportDuringDatabaseMove) {
  const base::Time now = base::Time::Now();
  const std::vector<CrashReportDescriptor> reports = {
      Report(kReportId, now - base::Minutes(1), 2 * kMiB),
      Report(kReportId, now - base::Minutes(1), 2 * kMiB),
  };

  CrashReportSelection selection =
      SelectCrashReports(reports, ExportScope::kLatestIncident, now);

  ASSERT_EQ(selection.selected.size(), 1u);
  EXPECT_EQ(selection.selected[0].local_id, kReportId);
  EXPECT_EQ(selection.selected_bytes, 2 * kMiB);
  EXPECT_TRUE(selection.omitted_ids.empty());
}

TEST(DiagnosticsExporterTest, RejectsReportOutsideDatabase) {
  CrashReportDescriptor report = Report(kReportId, base::Time::Now());
  EXPECT_TRUE(
      IsCrashReportPathSafe(base::FilePath(FILE_PATH_LITERAL("/db")), report));

  report.file_path = base::FilePath(FILE_PATH_LITERAL("/tmp/stolen.dmp"));
  EXPECT_FALSE(
      IsCrashReportPathSafe(base::FilePath(FILE_PATH_LITERAL("/db")), report));

  report.file_path =
      base::FilePath(FILE_PATH_LITERAL("/db/pending/not-a-dump.txt"));
  EXPECT_FALSE(
      IsCrashReportPathSafe(base::FilePath(FILE_PATH_LITERAL("/db")), report));

  report.file_path = base::FilePath(FILE_PATH_LITERAL("/db/pending"))
                         .AppendASCII("not-a-uuid.dmp");
  report.local_id = "not-a-uuid";
  EXPECT_FALSE(
      IsCrashReportPathSafe(base::FilePath(FILE_PATH_LITERAL("/db")), report));
}

TEST(DiagnosticsExporterTest, SensitiveHashUsesExportSalt) {
  const std::string first = HashSensitiveValue("salt-a", "proxy.example");
  EXPECT_EQ(first, HashSensitiveValue("salt-a", "proxy.example"));
  EXPECT_NE(first, HashSensitiveValue("salt-b", "proxy.example"));
  EXPECT_NE(first, HashSensitiveValue("salt-a", "other.example"));
  EXPECT_EQ(first.size(), 64u);
}

TEST(DiagnosticsExporterTest, RejectsMissingCrashpadDatabase) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath missing = temp_dir.GetPath().AppendASCII("missing");

  EXPECT_TRUE(GetCrashReportsFromCrashpadDatabase(base::FilePath()).empty());
  EXPECT_TRUE(GetCrashReportsFromCrashpadDatabase(missing).empty());
  EXPECT_FALSE(base::PathExists(missing));
}

TEST(DiagnosticsExporterTest, RejectsFileInsteadOfCrashpadDatabase) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath invalid = temp_dir.GetPath().AppendASCII("Crashpad");
  ASSERT_TRUE(base::WriteFile(invalid, "not-a-database"));

  EXPECT_TRUE(GetCrashReportsFromCrashpadDatabase(invalid).empty());
}

TEST(DiagnosticsExporterTest,
     ReadsPendingAndCompletedReportsFromCrashpadDatabase) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath database_path =
      temp_dir.GetPath().AppendASCII("Crashpad");
  std::unique_ptr<crashpad::CrashReportDatabase> database =
      crashpad::CrashReportDatabase::Initialize(database_path);
  ASSERT_TRUE(database);

  crashpad::UUID pending_uuid;
  crashpad::UUID completed_uuid;
  CreateCrashpadReport(database.get(), "MDMPpending", &pending_uuid);
  CreateCrashpadReport(database.get(), "MDMPcompleted", &completed_uuid);
  std::unique_ptr<const crashpad::CrashReportDatabase::UploadReport>
      upload_report;
  ASSERT_EQ(
      database->GetReportForUploading(completed_uuid, &upload_report, false),
      crashpad::CrashReportDatabase::kNoError);
  ASSERT_EQ(database->RecordUploadComplete(std::move(upload_report),
                                           "completed-report"),
            crashpad::CrashReportDatabase::kNoError);

  const std::vector<CrashReportDescriptor> reports =
      GetCrashReportsFromCrashpadDatabase(database_path);

  ASSERT_EQ(reports.size(), 2u);
  EXPECT_EQ(reports[0].local_id, pending_uuid.ToString());
  EXPECT_EQ(reports[1].local_id, completed_uuid.ToString());
  EXPECT_EQ(reports[0].file_path.Extension(), FILE_PATH_LITERAL(".dmp"));
  EXPECT_EQ(reports[1].file_path.Extension(), FILE_PATH_LITERAL(".dmp"));
  EXPECT_TRUE(IsCrashReportPathSafe(database_path, reports[0]));
  EXPECT_TRUE(IsCrashReportPathSafe(database_path, reports[1]));
  EXPECT_GT(reports[0].size, 0u);
  EXPECT_GT(reports[1].size, 0u);
}

TEST(DiagnosticsExporterTest, BuildsIntegrityCheckedArchive) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath database = temp_dir.GetPath().AppendASCII("Crashpad");
  const base::FilePath pending = database.AppendASCII("pending");
  ASSERT_TRUE(base::CreateDirectory(pending));
  const base::FilePath dump =
      pending.AppendASCII(std::string(kReportId) + ".dmp");
  ASSERT_TRUE(base::WriteFile(dump, "MDMPfake-minidump"));

  DiagnosticsBundleRequest request;
  request.now = base::Time::Now();
  request.target_path = temp_dir.GetPath().AppendASCII("diagnostics.zip");
  request.crashpad_database_path = database;
  request.crash_reports.push_back({.local_id = kReportId,
                                   .file_path = dump,
                                   .capture_time = request.now,
                                   .size = 13});
  base::DictValue browser_state;
  browser_state.Set("status", "ready");
  request.state_files.emplace("browser", std::move(browser_state));
  request.product_name = "Fingerprint Browser";
  request.product_version = "1.0";
  request.source_revision = "revision";

  DiagnosticsExportResult result = BuildDiagnosticsBundle(request);

  ASSERT_TRUE(result.success) << result.error;
  EXPECT_EQ(result.crash_count, 1u);
  zip::ZipReader reader;
  ASSERT_TRUE(reader.Open(result.archive_path));
  std::vector<std::string> entries;
  while (const auto* entry = reader.Next()) {
    entries.push_back(entry->path.AsUTF8Unsafe());
  }
  EXPECT_THAT(entries,
              testing::Contains(std::string("crashes/") + kReportId + ".dmp"));
  EXPECT_THAT(entries, testing::Contains("state/browser.json"));
  EXPECT_THAT(entries, testing::Contains("manifest.json"));
  EXPECT_THAT(entries, testing::Contains("checksums.sha256"));
}

TEST(DiagnosticsExporterTest, RejectsForbiddenTextBeforeArchiveCreation) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath database = temp_dir.GetPath().AppendASCII("Crashpad");
  ASSERT_TRUE(base::CreateDirectory(database));

  DiagnosticsBundleRequest request;
  request.now = base::Time::Now();
  request.target_path = temp_dir.GetPath().AppendASCII("diagnostics.zip");
  request.crashpad_database_path = database;
  base::DictValue proxy_state;
  proxy_state.Set("password", "secret-canary");
  request.state_files.emplace("proxy", std::move(proxy_state));
  request.forbidden_text_values.push_back("secret-canary");

  DiagnosticsExportResult result = BuildDiagnosticsBundle(request);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error, "forbidden_text_detected");
  EXPECT_FALSE(base::PathExists(request.target_path));
}

TEST(DiagnosticsExporterTest, BuildsArchiveWithoutCrashReports) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  DiagnosticsBundleRequest request;
  request.now = base::Time::Now();
  request.target_path = temp_dir.GetPath().AppendASCII("diagnostics.zip");
  base::DictValue browser_state;
  browser_state.Set("status", "ready");
  request.state_files.emplace("browser", std::move(browser_state));

  DiagnosticsExportResult result = BuildDiagnosticsBundle(request);

  ASSERT_TRUE(result.success) << result.error;
  EXPECT_EQ(result.crash_count, 0u);
  EXPECT_TRUE(base::PathExists(result.archive_path));
}

TEST(DiagnosticsExporterTest, OmitsCorruptCrashReport) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath database = temp_dir.GetPath().AppendASCII("Crashpad");
  const base::FilePath pending = database.AppendASCII("pending");
  ASSERT_TRUE(base::CreateDirectory(pending));
  const base::FilePath dump =
      pending.AppendASCII(std::string(kReportId) + ".dmp");
  ASSERT_TRUE(base::WriteFile(dump, "not-a-minidump"));

  DiagnosticsBundleRequest request;
  request.now = base::Time::Now();
  request.target_path = temp_dir.GetPath().AppendASCII("diagnostics.zip");
  request.crashpad_database_path = database;
  request.crash_reports.push_back({.local_id = kReportId,
                                   .file_path = dump,
                                   .capture_time = request.now,
                                   .size = 14});

  DiagnosticsExportResult result = BuildDiagnosticsBundle(request);

  ASSERT_TRUE(result.success) << result.error;
  EXPECT_EQ(result.crash_count, 0u);
  EXPECT_THAT(result.omitted_report_ids, testing::Contains(kReportId));
}

#if BUILDFLAG(IS_POSIX)
TEST(DiagnosticsExporterTest, OmitsCrashReportSymlinkOutsideDatabase) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath database = temp_dir.GetPath().AppendASCII("Crashpad");
  const base::FilePath pending = database.AppendASCII("pending");
  const base::FilePath outside = temp_dir.GetPath().AppendASCII("outside");
  ASSERT_TRUE(base::CreateDirectory(pending));
  ASSERT_TRUE(base::CreateDirectory(outside));
  const base::FilePath outside_dump =
      outside.AppendASCII(std::string(kReportId) + ".dmp");
  const base::FilePath linked_dump =
      pending.AppendASCII(std::string(kReportId) + ".dmp");
  ASSERT_TRUE(base::WriteFile(outside_dump, "MDMPoutside"));
  ASSERT_TRUE(base::CreateSymbolicLink(outside_dump, linked_dump));

  DiagnosticsBundleRequest request;
  request.now = base::Time::Now();
  request.target_path = temp_dir.GetPath().AppendASCII("diagnostics.zip");
  request.crashpad_database_path = database;
  request.crash_reports.push_back({.local_id = kReportId,
                                   .file_path = linked_dump,
                                   .capture_time = request.now,
                                   .size = 11});

  DiagnosticsExportResult result = BuildDiagnosticsBundle(request);

  ASSERT_TRUE(result.success) << result.error;
  EXPECT_EQ(result.crash_count, 0u);
  EXPECT_THAT(result.omitted_report_ids, testing::Contains(kReportId));
}
#endif

TEST(DiagnosticsExporterTest, LeavesNoPartialArchiveWhenPlacementFails) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath target =
      temp_dir.GetPath().AppendASCII("missing").AppendASCII("diagnostics.zip");

  DiagnosticsBundleRequest request;
  request.now = base::Time::Now();
  request.target_path = target;

  DiagnosticsExportResult result = BuildDiagnosticsBundle(request);

  EXPECT_FALSE(result.success);
  EXPECT_FALSE(base::PathExists(target));
}

TEST(DiagnosticsExporterTest, RetriesMovedCrashReportByUuid) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath database = temp_dir.GetPath().AppendASCII("Crashpad");
  const base::FilePath pending = database.AppendASCII("pending");
  const base::FilePath completed = database.AppendASCII("completed");
  ASSERT_TRUE(base::CreateDirectory(pending));
  ASSERT_TRUE(base::CreateDirectory(completed));

  const base::Time now = base::Time::Now();
  const base::FilePath moved_dump =
      completed.AppendASCII(std::string(kReportId) + ".dmp");
  ASSERT_TRUE(base::WriteFile(moved_dump, "MDMPmoved-minidump"));

  DiagnosticsBundleRequest request;
  request.now = now;
  request.target_path = temp_dir.GetPath().AppendASCII("diagnostics.zip");
  request.crashpad_database_path = database;
  request.crash_reports.push_back(
      {.local_id = kReportId,
       .file_path = pending.AppendASCII(std::string(kReportId) + ".dmp"),
       .capture_time = now,
       .size = 14});
  request.refresh_crash_reports = base::BindRepeating(
      [](base::FilePath path, base::Time capture_time) {
        return std::vector<CrashReportDescriptor>({
            {.local_id = kReportId,
             .file_path = std::move(path),
             .capture_time = capture_time,
             .size = 14},
        });
      },
      moved_dump, now);

  DiagnosticsExportResult result = BuildDiagnosticsBundle(request);

  ASSERT_TRUE(result.success) << result.error;
  EXPECT_EQ(result.crash_count, 1u);
}

TEST(DiagnosticsExporterTest, RedactsDebugLogBeforePackaging) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath database = temp_dir.GetPath().AppendASCII("Crashpad");
  ASSERT_TRUE(base::CreateDirectory(database));
  const base::FilePath debug_log = temp_dir.GetPath().AppendASCII("debug.log");
  constexpr std::string_view kEmail = "tester@example.com";
  constexpr std::string_view kIp = "203.0.113.42";
  ASSERT_TRUE(base::WriteFile(
      debug_log,
      "account=tester@example.com proxy=203.0.113.42 url=https://example.com"));

  DiagnosticsBundleRequest request;
  request.now = base::Time::Now();
  request.target_path = temp_dir.GetPath().AppendASCII("diagnostics.zip");
  request.crashpad_database_path = database;
  request.debug_logs.push_back(debug_log);
  request.forbidden_text_values.emplace_back(kEmail);
  request.forbidden_text_values.emplace_back(kIp);

  DiagnosticsExportResult result = BuildDiagnosticsBundle(request);

  ASSERT_TRUE(result.success) << result.error;
  zip::ZipReader reader;
  ASSERT_TRUE(reader.Open(result.archive_path));
  bool found_log = false;
  while (const auto* entry = reader.Next()) {
    if (entry->path.AsUTF8Unsafe() != "logs/chrome-debug-redacted.log") {
      continue;
    }
    std::string contents;
    ASSERT_TRUE(reader.ExtractCurrentEntryToString(&contents));
    EXPECT_EQ(contents.find(kEmail), std::string::npos);
    EXPECT_EQ(contents.find(kIp), std::string::npos);
    found_log = true;
  }
  EXPECT_TRUE(found_log);
}

}  // namespace
}  // namespace fingerprint_browser::diagnostics
