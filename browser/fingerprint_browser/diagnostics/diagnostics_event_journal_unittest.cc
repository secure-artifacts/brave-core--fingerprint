/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/fingerprint_browser/diagnostics/diagnostics_event_journal.h"

#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace fingerprint_browser::diagnostics {
namespace {

class DiagnosticsEventJournalTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
};

TEST_F(DiagnosticsEventJournalTest, WritesOnlyTypedFields) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::Time now = base::Time::Now();
  DiagnosticsEventFields fields;
  fields.status = "active";
  fields.proxy_scheme = "http";
  fields.country = "DE";
  fields.persona_schema = 3;

  ASSERT_TRUE(RecordDiagnosticsEvent(temp_dir.GetPath(),
                                     DiagnosticsEventType::kProxyStateChanged,
                                     fields, now));

  const auto logs = GetDiagnosticsEventLogs(temp_dir.GetPath(), now);
  ASSERT_EQ(logs.size(), 1u);
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(logs[0], &contents));
  EXPECT_NE(contents.find("proxy_state_changed"), std::string::npos);
  EXPECT_NE(contents.find("\"country\":\"DE\""), std::string::npos);
  EXPECT_EQ(contents.find("password"), std::string::npos);
}

TEST_F(DiagnosticsEventJournalTest, RemovesFilesOlderThanSevenDays) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::Time now = base::Time::Now();
  ASSERT_TRUE(RecordDiagnosticsEvent(temp_dir.GetPath(),
                                     DiagnosticsEventType::kError, {},
                                     now - base::Days(8)));
  const auto old_logs =
      GetDiagnosticsEventLogs(temp_dir.GetPath(), now - base::Days(8));
  ASSERT_EQ(old_logs.size(), 1u);
  ASSERT_TRUE(
      base::TouchFile(old_logs[0], now - base::Days(8), now - base::Days(8)));
  ASSERT_TRUE(RecordDiagnosticsEvent(
      temp_dir.GetPath(), DiagnosticsEventType::kBrowserStarted, {}, now));

  const auto logs = GetDiagnosticsEventLogs(temp_dir.GetPath(), now);
  ASSERT_EQ(logs.size(), 1u);
  EXPECT_NE(logs[0].BaseName().AsUTF8Unsafe().find("fingerprint-events-"),
            std::string::npos);
}

TEST_F(DiagnosticsEventJournalTest, RotatesAtFiveMiB) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::Time now = base::Time::Now();
  ASSERT_TRUE(RecordDiagnosticsEvent(
      temp_dir.GetPath(), DiagnosticsEventType::kBrowserStarted, {}, now));
  const auto initial_logs = GetDiagnosticsEventLogs(temp_dir.GetPath(), now);
  ASSERT_EQ(initial_logs.size(), 1u);
  ASSERT_TRUE(
      base::WriteFile(initial_logs[0], std::string(5 * 1024 * 1024, 'x')));

  ASSERT_TRUE(RecordDiagnosticsEvent(
      temp_dir.GetPath(), DiagnosticsEventType::kBrowserStopping, {}, now));

  const auto logs = GetDiagnosticsEventLogs(temp_dir.GetPath(), now);
  EXPECT_EQ(logs.size(), 2u);
}

}  // namespace
}  // namespace fingerprint_browser::diagnostics
