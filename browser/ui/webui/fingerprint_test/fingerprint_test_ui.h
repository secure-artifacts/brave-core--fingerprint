/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_WEBUI_FINGERPRINT_TEST_FINGERPRINT_TEST_UI_H_
#define BRAVE_BROWSER_UI_WEBUI_FINGERPRINT_TEST_FINGERPRINT_TEST_UI_H_

#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"
#include "content/public/common/url_constants.h"

inline constexpr char kFingerprintTestHost[] = "fingerprint-test";

class FingerprintTestUI;

class FingerprintTestUIConfig
    : public content::DefaultWebUIConfig<FingerprintTestUI> {
 public:
  FingerprintTestUIConfig()
      : DefaultWebUIConfig(content::kChromeUIScheme, kFingerprintTestHost) {}
};

class FingerprintTestUI : public content::WebUIController {
 public:
  explicit FingerprintTestUI(content::WebUI* web_ui);
  FingerprintTestUI(const FingerprintTestUI&) = delete;
  FingerprintTestUI& operator=(const FingerprintTestUI&) = delete;
  ~FingerprintTestUI() override;
};

#endif  // BRAVE_BROWSER_UI_WEBUI_FINGERPRINT_TEST_FINGERPRINT_TEST_UI_H_
