/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_WEBUI_FINGERPRINT_GUIDE_FINGERPRINT_GUIDE_UI_H_
#define BRAVE_BROWSER_UI_WEBUI_FINGERPRINT_GUIDE_FINGERPRINT_GUIDE_UI_H_

#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"
#include "content/public/common/url_constants.h"

inline constexpr char kFingerprintGuideHost[] = "fingerprint-guide";

class FingerprintGuideUI;

class FingerprintGuideUIConfig
    : public content::DefaultWebUIConfig<FingerprintGuideUI> {
 public:
  FingerprintGuideUIConfig()
      : DefaultWebUIConfig(content::kChromeUIScheme, kFingerprintGuideHost) {}
};

class FingerprintGuideUI : public content::WebUIController {
 public:
  explicit FingerprintGuideUI(content::WebUI* web_ui);
  FingerprintGuideUI(const FingerprintGuideUI&) = delete;
  FingerprintGuideUI& operator=(const FingerprintGuideUI&) = delete;
  ~FingerprintGuideUI() override;
};

#endif  // BRAVE_BROWSER_UI_WEBUI_FINGERPRINT_GUIDE_FINGERPRINT_GUIDE_UI_H_
