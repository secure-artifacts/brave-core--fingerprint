/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_WEBUI_SETTINGS_FINGERPRINT_PROFILE_PROXY_HANDLER_H_
#define BRAVE_BROWSER_UI_WEBUI_SETTINGS_FINGERPRINT_PROFILE_PROXY_HANDLER_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "brave/browser/fingerprint_browser/fingerprint_proxy_service.h"
#include "chrome/browser/ui/webui/settings/settings_page_ui_handler.h"

class Profile;

class FingerprintProfileProxyHandler
    : public settings::SettingsPageUIHandler,
      public fingerprint_browser::FingerprintProxyService::Observer {
 public:
  FingerprintProfileProxyHandler();
  FingerprintProfileProxyHandler(const FingerprintProfileProxyHandler&) =
      delete;
  FingerprintProfileProxyHandler& operator=(
      const FingerprintProfileProxyHandler&) = delete;
  ~FingerprintProfileProxyHandler() override;

 private:
  void RegisterMessages() override;
  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;

  void GetState(const base::ListValue& args);
  void VerifyDraft(const base::ListValue& args);
  void ApplyVerified(const base::ListValue& args);
  void Revalidate(const base::ListValue& args);
  void Disable(const base::ListValue& args);

  void OnVerificationComplete(
      base::Value callback_id,
      fingerprint_browser::ProxyVerificationResult result);
  void OnApplyComplete(base::Value callback_id,
                       fingerprint_browser::ProxyApplyResult result);
  void OnDisableComplete(base::Value callback_id);

  void OnFingerprintProxyStateChanged() override;

  base::DictValue BuildState() const;
  base::DictValue BuildVerificationResult(
      const fingerprint_browser::ProxyVerificationResult& result) const;
  base::DictValue BuildGeo(
      const fingerprint_browser::ProfileProxyGeo& geo) const;

  raw_ptr<Profile> profile_ = nullptr;
  raw_ptr<fingerprint_browser::FingerprintProxyService> service_ = nullptr;
  bool observing_service_ = false;
  base::WeakPtrFactory<FingerprintProfileProxyHandler> weak_factory_{this};
};

#endif  // BRAVE_BROWSER_UI_WEBUI_SETTINGS_FINGERPRINT_PROFILE_PROXY_HANDLER_H_
