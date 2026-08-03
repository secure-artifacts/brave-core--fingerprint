/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/webui/fingerprint_guide/fingerprint_guide_ui.h"

#include "brave/browser/ui/webui/brave_webui_source.h"
#include "brave/components/fingerprint_browser/resources/grit/fingerprint_test_generated_map.h"
#include "brave/components/fingerprint_browser/resources/grit/fingerprint_test_resources.h"
#include "content/public/browser/web_ui.h"

FingerprintGuideUI::FingerprintGuideUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  CreateAndAddWebUIDataSource(web_ui, kFingerprintGuideHost,
                              kFingerprintTestGenerated,
                              IDR_FINGERPRINT_GUIDE_HTML);
}

FingerprintGuideUI::~FingerprintGuideUI() = default;
