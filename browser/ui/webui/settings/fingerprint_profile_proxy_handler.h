/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_WEBUI_SETTINGS_FINGERPRINT_PROFILE_PROXY_HANDLER_H_
#define BRAVE_BROWSER_UI_WEBUI_SETTINGS_FINGERPRINT_PROFILE_PROXY_HANDLER_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/values.h"
#include "chrome/browser/ui/webui/settings/settings_page_ui_handler.h"
#include "components/prefs/pref_change_registrar.h"

class Profile;

class FingerprintProfileProxyHandler : public settings::SettingsPageUIHandler {
 public:
  FingerprintProfileProxyHandler();
  FingerprintProfileProxyHandler(const FingerprintProfileProxyHandler&) =
      delete;
  FingerprintProfileProxyHandler& operator=(
      const FingerprintProfileProxyHandler&) = delete;
  ~FingerprintProfileProxyHandler() override;

 private:
  void RegisterMessages() override;
  void OnJavascriptAllowed() override {}
  void OnJavascriptDisallowed() override {}

  void GetConfig(const base::ListValue& args);
  void SetConfig(const base::ListValue& args);
  void GetLastError(const base::ListValue& args);
  void OnLastErrorChanged();

  base::DictValue BuildConfig() const;
  base::DictValue BuildLastError() const;
  std::string ValidateConfig(const base::DictValue& config) const;

  raw_ptr<Profile> profile_ = nullptr;
  PrefChangeRegistrar pref_change_registrar_;
};

#endif  // BRAVE_BROWSER_UI_WEBUI_SETTINGS_FINGERPRINT_PROFILE_PROXY_HANDLER_H_
