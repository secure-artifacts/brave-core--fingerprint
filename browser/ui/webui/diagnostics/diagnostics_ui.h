/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_WEBUI_DIAGNOSTICS_DIAGNOSTICS_UI_H_
#define BRAVE_BROWSER_UI_WEBUI_DIAGNOSTICS_DIAGNOSTICS_UI_H_

#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"
#include "content/public/common/url_constants.h"

inline constexpr char kDiagnosticsHost[] = "diagnostics";

class DiagnosticsUI;

class DiagnosticsUIConfig : public content::DefaultWebUIConfig<DiagnosticsUI> {
 public:
  DiagnosticsUIConfig()
      : DefaultWebUIConfig(content::kChromeUIScheme, kDiagnosticsHost) {}

  bool IsWebUIEnabled(content::BrowserContext* browser_context) override;
};

class DiagnosticsUI : public content::WebUIController {
 public:
  explicit DiagnosticsUI(content::WebUI* web_ui);
  DiagnosticsUI(const DiagnosticsUI&) = delete;
  DiagnosticsUI& operator=(const DiagnosticsUI&) = delete;
  ~DiagnosticsUI() override;
};

#endif  // BRAVE_BROWSER_UI_WEBUI_DIAGNOSTICS_DIAGNOSTICS_UI_H_
