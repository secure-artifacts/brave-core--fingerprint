/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/webui/diagnostics/diagnostics_ui.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/debug/alias.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/weak_ptr.h"
#include "base/immediate_crash.h"
#include "base/profiler/module_cache.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/version_info/version_info.h"
#include "brave/browser/fingerprint_browser/diagnostics/diagnostics_data_collector.h"
#include "brave/browser/fingerprint_browser/diagnostics/diagnostics_event_journal.h"
#include "brave/browser/fingerprint_browser/diagnostics/diagnostics_exporter.h"
#include "brave/browser/ui/webui/brave_webui_source.h"
#include "brave/components/fingerprint_browser/resources/grit/fingerprint_test_generated_map.h"
#include "brave/components/fingerprint_browser/resources/grit/fingerprint_test_resources.h"
#include "brave/grit/brave_generated_resources.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/select_file_policy/chrome_select_file_policy.h"
#include "components/crash/core/app/crashpad.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/shell_dialogs/selected_file_info.h"

namespace fingerprint_browser::diagnostics {

NOINLINE void CrashBrowserProcessForDiagnosticsTesting() {
  NO_CODE_FOLDING();
  base::ImmediateCrash();
}

}  // namespace fingerprint_browser::diagnostics

namespace {

using fingerprint_browser::diagnostics::BuildDiagnosticsBundle;
using fingerprint_browser::diagnostics::CollectDiagnosticsProfileData;
using fingerprint_browser::diagnostics::CollectedDiagnosticsData;
using fingerprint_browser::diagnostics::CompleteDiagnosticsData;
using fingerprint_browser::diagnostics::CrashReportDescriptor;
using fingerprint_browser::diagnostics::DiagnosticsBundleRequest;
using fingerprint_browser::diagnostics::DiagnosticsExportResult;
using fingerprint_browser::diagnostics::ExportScope;
using fingerprint_browser::diagnostics::GetCrashReportsFromCrashpad;
using fingerprint_browser::diagnostics::GetDiagnosticsEventTaskRunner;

std::optional<ExportScope> ParseExportScope(std::string_view value) {
  if (value == "latest_incident") {
    return ExportScope::kLatestIncident;
  }
  if (value == "last_7_days") {
    return ExportScope::kLastSevenDays;
  }
  return std::nullopt;
}

std::string CreateExportSalt() {
  return base::HexEncodeLower(base::RandBytesAsVector(32));
}

std::string BuildIdentityForArchiveName() {
  std::string identity(version_info::GetLastChange());
  identity.erase(std::remove_if(identity.begin(), identity.end(),
                                [](char value) {
                                  return !base::IsAsciiAlphaNumeric(value);
                                }),
                 identity.end());
  if (identity.empty() ||
      std::ranges::all_of(identity, [](char value) { return value == '0'; })) {
    base::ModuleCache cache;
    const auto* module = cache.GetModuleForAddress(
        reinterpret_cast<uintptr_t>(&BuildIdentityForArchiveName));
    identity = module ? module->GetId() : std::string();
  }
  if (identity.size() > 12) {
    identity.resize(12);
  }
  return identity.empty() ? "unknown" : identity;
}

std::string BuildArchiveName(base::Time now) {
  base::Time::Exploded exploded;
  now.LocalExplode(&exploded);
  const std::string build_identity = BuildIdentityForArchiveName();
  return base::StringPrintf(
      "Fingerprint-Browser-Diagnostics-%04d%02d%02d-%02d%02d%02d-%s.zip",
      exploded.year, exploded.month, exploded.day_of_month, exploded.hour,
      exploded.minute, exploded.second, build_identity.c_str());
}

base::DictValue ExportResultToValue(const DiagnosticsExportResult& result) {
  base::DictValue value;
  value.Set("success", result.success);
  value.Set("cancelled", false);
  value.Set("error", result.error);
  value.Set("crashCount", static_cast<int>(result.crash_count));
  value.Set("archiveName", result.archive_path.BaseName().AsUTF8Unsafe());
  value.Set("omittedCrashCount",
            static_cast<int>(result.omitted_report_ids.size()));
  return value;
}

DiagnosticsExportResult CollectAndBuildDiagnosticsBundle(
    CollectedDiagnosticsData data,
    base::FilePath user_data_dir,
    ExportScope scope,
    base::Time now,
    base::FilePath target_path) {
  std::vector<CrashReportDescriptor> crash_reports =
      GetCrashReportsFromCrashpad();
  data = CompleteDiagnosticsData(std::move(data), user_data_dir, scope, now,
                                 crash_reports);
  DiagnosticsBundleRequest request;
  request.scope = scope;
  request.now = now;
  request.target_path = std::move(target_path);
  request.crashpad_database_path =
      crash_reporter::GetCrashpadDatabasePath().value_or(base::FilePath());
  request.crash_reports = std::move(crash_reports);
  request.native_reports = std::move(data.native_reports);
  request.event_logs = std::move(data.event_logs);
  request.debug_logs = std::move(data.debug_logs);
  request.state_files = std::move(data.state_files);
  request.forbidden_text_values = std::move(data.forbidden_text_values);
  request.refresh_crash_reports =
      base::BindRepeating(&GetCrashReportsFromCrashpad);
  request.product_name = std::move(data.product_name);
  request.product_version = std::move(data.product_version);
  request.source_revision = std::move(data.source_revision);
  request.module_id = std::move(data.module_id);
  request.module_path = std::move(data.module_path);
  return BuildDiagnosticsBundle(std::move(request));
}

base::DictValue CollectDiagnosticsState(bool can_export,
                                        bool guest,
                                        bool exporting,
                                        base::FilePath last_export_path) {
  const auto crashpad_path = crash_reporter::GetCrashpadDatabasePath();
  base::DictValue state;
  state.Set("canExport", can_export);
  state.Set("guest", guest);
  state.Set("exporting", exporting);
  state.Set("crashFolderAvailable",
            crashpad_path && base::DirectoryExists(*crashpad_path));
  state.Set("lastExportAvailable",
            !last_export_path.empty() && base::PathExists(last_export_path));
  state.Set("localCrashCount",
            static_cast<int>(GetCrashReportsFromCrashpad().size()));
  state.Set("defaultScope", "latest_incident");
  return state;
}

class DiagnosticsMessageHandler : public content::WebUIMessageHandler,
                                  public ui::SelectFileDialog::Listener {
 public:
  DiagnosticsMessageHandler() = default;
  DiagnosticsMessageHandler(const DiagnosticsMessageHandler&) = delete;
  DiagnosticsMessageHandler& operator=(const DiagnosticsMessageHandler&) =
      delete;

  ~DiagnosticsMessageHandler() override {
    if (select_file_dialog_) {
      select_file_dialog_->ListenerDestroyed();
    }
  }

  void RegisterMessages() override {
    web_ui()->RegisterMessageCallback(
        "getDiagnosticsState",
        base::BindRepeating(&DiagnosticsMessageHandler::HandleGetState,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "exportDiagnosticsBundle",
        base::BindRepeating(&DiagnosticsMessageHandler::HandleExportBundle,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "openDiagnosticsCrashFolder",
        base::BindRepeating(&DiagnosticsMessageHandler::HandleOpenCrashFolder,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "openDiagnosticsExportFolder",
        base::BindRepeating(&DiagnosticsMessageHandler::HandleOpenExportFolder,
                            base::Unretained(this)));
    if (base::CommandLine::ForCurrentProcess()->HasSwitch(
            switches::kEnableCrashReporterForTesting)) {
      web_ui()->RegisterMessageCallback(
          "crashDiagnosticsForTesting",
          base::BindRepeating(
              &DiagnosticsMessageHandler::HandleCrashForTesting,
              base::Unretained(this)));
    }
  }

  void FileSelected(const ui::SelectedFileInfo& file, int index) override {
    select_file_dialog_ = nullptr;
    Profile* profile = GetExportProfile();
    if (!profile || pending_callback_id_.is_none() || !pending_scope_) {
      ResolvePendingError("profile_unavailable");
      return;
    }

    profile->set_last_selected_directory(file.path().DirName());
    const base::Time now = base::Time::Now();
    const ExportScope scope = *pending_scope_;
    CollectedDiagnosticsData data =
        CollectDiagnosticsProfileData(profile, CreateExportSalt());
    const base::FilePath user_data_dir = profile->GetPath().DirName();

    GetDiagnosticsEventTaskRunner()->PostTaskAndReplyWithResult(
        FROM_HERE,
        base::BindOnce(&CollectAndBuildDiagnosticsBundle, std::move(data),
                       user_data_dir, scope, now, file.path()),
        base::BindOnce(&DiagnosticsMessageHandler::OnExportFinished,
                       weak_ptr_factory_.GetWeakPtr()));
  }

  void FileSelectionCanceled() override {
    select_file_dialog_ = nullptr;
    if (pending_callback_id_.is_none()) {
      return;
    }
    base::DictValue result;
    result.Set("success", false);
    result.Set("cancelled", true);
    result.Set("error", "");
    ResolveJavascriptCallback(pending_callback_id_,
                              base::Value(std::move(result)));
    ResetPendingExport();
  }

 private:
  Profile* GetExportProfile() {
    Profile* profile = Profile::FromWebUI(web_ui());
    if (!profile || profile->IsGuestSession()) {
      return nullptr;
    }
    Profile* original = profile->GetOriginalProfile();
    return original->IsRegularProfile() && !original->IsTor() ? original
                                                              : nullptr;
  }

  void HandleGetState(const base::ListValue& args) {
    AllowJavascript();
    base::Value callback_id = args[0].Clone();
    Profile* profile = GetExportProfile();
    Profile* web_ui_profile = Profile::FromWebUI(web_ui());
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&CollectDiagnosticsState, profile != nullptr,
                       web_ui_profile && web_ui_profile->IsGuestSession(),
                       !pending_callback_id_.is_none(), last_export_path_),
        base::BindOnce(&DiagnosticsMessageHandler::OnGetState,
                       weak_ptr_factory_.GetWeakPtr(), std::move(callback_id)));
  }

  void HandleExportBundle(const base::ListValue& args) {
    AllowJavascript();
    const base::Value& callback_id = args[0];
    if (args.size() != 2 || !args[1].is_string()) {
      RejectJavascriptCallback(callback_id, base::Value("invalid_scope"));
      return;
    }
    const auto scope = ParseExportScope(args[1].GetString());
    if (!scope) {
      RejectJavascriptCallback(callback_id, base::Value("invalid_scope"));
      return;
    }
    Profile* profile = GetExportProfile();
    if (!profile) {
      RejectJavascriptCallback(callback_id, base::Value("profile_not_allowed"));
      return;
    }
    if (!pending_callback_id_.is_none() || select_file_dialog_) {
      RejectJavascriptCallback(callback_id,
                               base::Value("export_already_running"));
      return;
    }

    pending_callback_id_ = callback_id.Clone();
    pending_scope_ = scope;
    select_file_dialog_ = ui::SelectFileDialog::Create(
        this,
        std::make_unique<ChromeSelectFilePolicy>(web_ui()->GetWebContents()));
    if (!select_file_dialog_) {
      ResolvePendingError("save_dialog_unavailable");
      return;
    }

    ui::SelectFileDialog::FileTypeInfo file_types;
    file_types.allowed_paths = ui::SelectFileDialog::FileTypeInfo::NATIVE_PATH;
    file_types.extensions.resize(1);
    file_types.extensions[0].push_back(FILE_PATH_LITERAL("zip"));
    const base::FilePath suggested =
        profile->last_selected_directory().AppendASCII(
            BuildArchiveName(base::Time::Now()));
    select_file_dialog_->SelectFile(
        ui::SelectFileDialog::SELECT_SAVEAS_FILE, std::u16string(), suggested,
        &file_types, 0, base::FilePath::StringType(),
        web_ui()->GetWebContents()->GetTopLevelNativeWindow(), nullptr);
  }

  void HandleOpenCrashFolder(const base::ListValue& args) {
    Profile* profile = GetExportProfile();
    const auto path = crash_reporter::GetCrashpadDatabasePath();
    if (profile && path) {
      platform_util::OpenItem(profile, *path, platform_util::OPEN_FOLDER,
                              base::DoNothing());
    }
  }

  void HandleOpenExportFolder(const base::ListValue& args) {
    Profile* profile = GetExportProfile();
    if (profile && !last_export_path_.empty()) {
      platform_util::ShowItemInFolder(profile, last_export_path_);
      return;
    }
    if (profile && !profile->last_selected_directory().empty()) {
      platform_util::OpenItem(profile, profile->last_selected_directory(),
                              platform_util::OPEN_FOLDER, base::DoNothing());
    }
  }

  void HandleCrashForTesting(const base::ListValue& args) {
    fingerprint_browser::diagnostics::
        CrashBrowserProcessForDiagnosticsTesting();
  }

  void OnExportFinished(DiagnosticsExportResult result) {
    if (pending_callback_id_.is_none()) {
      return;
    }
    if (result.success) {
      last_export_path_ = result.archive_path;
      if (Profile* profile = GetExportProfile()) {
        fingerprint_browser::diagnostics::DiagnosticsEventFields fields;
        fields.status = "success";
        fingerprint_browser::diagnostics::RecordDiagnosticsEvent(
            profile->GetPath().DirName(),
            fingerprint_browser::diagnostics::DiagnosticsEventType::
                kDiagnosticsExported,
            fields);
      }
    }
    ResolveJavascriptCallback(pending_callback_id_,
                              base::Value(ExportResultToValue(result)));
    ResetPendingExport();
  }

  void OnGetState(base::Value callback_id, base::DictValue state) {
    ResolveJavascriptCallback(callback_id, base::Value(std::move(state)));
  }

  void ResolvePendingError(std::string error) {
    if (pending_callback_id_.is_none()) {
      return;
    }
    DiagnosticsExportResult result;
    result.error = std::move(error);
    ResolveJavascriptCallback(pending_callback_id_,
                              base::Value(ExportResultToValue(result)));
    ResetPendingExport();
  }

  void ResetPendingExport() {
    pending_callback_id_ = base::Value();
    pending_scope_.reset();
  }

  scoped_refptr<ui::SelectFileDialog> select_file_dialog_;
  base::Value pending_callback_id_;
  std::optional<ExportScope> pending_scope_;
  base::FilePath last_export_path_;
  base::WeakPtrFactory<DiagnosticsMessageHandler> weak_ptr_factory_{this};
};

}  // namespace

bool DiagnosticsUIConfig::IsWebUIEnabled(
    content::BrowserContext* browser_context) {
  Profile* profile =
      Profile::FromBrowserContext(browser_context)->GetOriginalProfile();
  return profile->IsRegularProfile() && !profile->IsGuestSession() &&
         !profile->IsTor();
}

DiagnosticsUI::DiagnosticsUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  web_ui->AddMessageHandler(std::make_unique<DiagnosticsMessageHandler>());
  auto* source = CreateAndAddWebUIDataSource(web_ui, kDiagnosticsHost,
                                             kFingerprintTestGenerated,
                                             IDR_DIAGNOSTICS_HTML);
  source->AddLocalizedString("diagnosticsPageTitle",
                             IDS_DIAGNOSTICS_PAGE_TITLE);
  source->AddLocalizedString("diagnosticsPageDescription",
                             IDS_DIAGNOSTICS_PAGE_DESCRIPTION);
  source->AddLocalizedString("diagnosticsExportHeading",
                             IDS_DIAGNOSTICS_EXPORT_HEADING);
  source->AddLocalizedString("diagnosticsExportDescription",
                             IDS_DIAGNOSTICS_EXPORT_DESCRIPTION);
  source->AddLocalizedString("diagnosticsChecking", IDS_DIAGNOSTICS_CHECKING);
  source->AddLocalizedString("diagnosticsTimeRange",
                             IDS_DIAGNOSTICS_TIME_RANGE);
  source->AddLocalizedString("diagnosticsLatestIncident",
                             IDS_DIAGNOSTICS_LATEST_INCIDENT);
  source->AddLocalizedString("diagnosticsLatestIncidentDescription",
                             IDS_DIAGNOSTICS_LATEST_INCIDENT_DESCRIPTION);
  source->AddLocalizedString("diagnosticsLastSevenDays",
                             IDS_DIAGNOSTICS_LAST_SEVEN_DAYS);
  source->AddLocalizedString("diagnosticsLastSevenDaysDescription",
                             IDS_DIAGNOSTICS_LAST_SEVEN_DAYS_DESCRIPTION);
  source->AddLocalizedString("diagnosticsMinidumpWarningTitle",
                             IDS_DIAGNOSTICS_MINIDUMP_WARNING_TITLE);
  source->AddLocalizedString("diagnosticsMinidumpWarningBody",
                             IDS_DIAGNOSTICS_MINIDUMP_WARNING_BODY);
  source->AddLocalizedString("diagnosticsMinidumpWarningShare",
                             IDS_DIAGNOSTICS_MINIDUMP_WARNING_SHARE);
  source->AddLocalizedString("diagnosticsPrivacyConfirm",
                             IDS_DIAGNOSTICS_PRIVACY_CONFIRM);
  source->AddLocalizedString("diagnosticsExportButton",
                             IDS_DIAGNOSTICS_EXPORT_BUTTON);
  source->AddLocalizedString("diagnosticsExportingButton",
                             IDS_DIAGNOSTICS_EXPORTING_BUTTON);
  source->AddLocalizedString("diagnosticsFilesHeading",
                             IDS_DIAGNOSTICS_FILES_HEADING);
  source->AddLocalizedString("diagnosticsFilesDescription",
                             IDS_DIAGNOSTICS_FILES_DESCRIPTION);
  source->AddLocalizedString("diagnosticsOpenCrashFolder",
                             IDS_DIAGNOSTICS_OPEN_CRASH_FOLDER);
  source->AddLocalizedString("diagnosticsOpenExportFolder",
                             IDS_DIAGNOSTICS_OPEN_EXPORT_FOLDER);
  source->AddLocalizedString("diagnosticsExportAlreadyRunning",
                             IDS_DIAGNOSTICS_EXPORT_ALREADY_RUNNING);
  source->AddLocalizedString("diagnosticsUnavailableGuest",
                             IDS_DIAGNOSTICS_UNAVAILABLE_GUEST);
  source->AddLocalizedString("diagnosticsUnavailableProfile",
                             IDS_DIAGNOSTICS_UNAVAILABLE_PROFILE);
  source->AddLocalizedString("diagnosticsAvailableReports",
                             IDS_DIAGNOSTICS_AVAILABLE_REPORTS);
  source->AddLocalizedString("diagnosticsLoadFailed",
                             IDS_DIAGNOSTICS_LOAD_FAILED);
  source->AddLocalizedString("diagnosticsLoadFailedTitle",
                             IDS_DIAGNOSTICS_LOAD_FAILED_TITLE);
  source->AddLocalizedString("diagnosticsReloadToRetry",
                             IDS_DIAGNOSTICS_RELOAD_TO_RETRY);
  source->AddLocalizedString("diagnosticsUnknownError",
                             IDS_DIAGNOSTICS_UNKNOWN_ERROR);
  source->AddLocalizedString("diagnosticsCreatingTitle",
                             IDS_DIAGNOSTICS_CREATING_TITLE);
  source->AddLocalizedString("diagnosticsCreatingMessage",
                             IDS_DIAGNOSTICS_CREATING_MESSAGE);
  source->AddLocalizedString("diagnosticsExportCanceledTitle",
                             IDS_DIAGNOSTICS_EXPORT_CANCELED_TITLE);
  source->AddLocalizedString("diagnosticsExportCanceledMessage",
                             IDS_DIAGNOSTICS_EXPORT_CANCELED_MESSAGE);
  source->AddLocalizedString("diagnosticsExportCompleteTitle",
                             IDS_DIAGNOSTICS_EXPORT_COMPLETE_TITLE);
  source->AddLocalizedString("diagnosticsExportCompleteMessage",
                             IDS_DIAGNOSTICS_EXPORT_COMPLETE_MESSAGE);
  source->AddLocalizedString("diagnosticsReportsOmitted",
                             IDS_DIAGNOSTICS_REPORTS_OMITTED);
  source->AddLocalizedString("diagnosticsExportFailedTitle",
                             IDS_DIAGNOSTICS_EXPORT_FAILED_TITLE);
  source->AddLocalizedString("diagnosticsExportFailedMessage",
                             IDS_DIAGNOSTICS_EXPORT_FAILED_MESSAGE);
  source->AddLocalizedString("diagnosticsOpeningCrashFolder",
                             IDS_DIAGNOSTICS_OPENING_CRASH_FOLDER);
  source->AddLocalizedString("diagnosticsOpeningExportFolder",
                             IDS_DIAGNOSTICS_OPENING_EXPORT_FOLDER);
}

DiagnosticsUI::~DiagnosticsUI() = default;
