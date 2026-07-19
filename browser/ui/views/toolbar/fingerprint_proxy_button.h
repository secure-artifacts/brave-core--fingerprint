/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_UI_VIEWS_TOOLBAR_FINGERPRINT_PROXY_BUTTON_H_
#define BRAVE_BROWSER_UI_VIEWS_TOOLBAR_FINGERPRINT_PROXY_BUTTON_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "brave/browser/fingerprint_browser/fingerprint_proxy_service.h"
#include "chrome/browser/ui/views/toolbar/toolbar_button.h"
#include "ui/base/metadata/metadata_header_macros.h"

class Browser;

namespace views {
class Widget;
}

class FingerprintProxyButton
    : public ToolbarButton,
      public fingerprint_browser::FingerprintProxyService::Observer {
  METADATA_HEADER(FingerprintProxyButton, ToolbarButton)

 public:
  explicit FingerprintProxyButton(Browser* browser);
  FingerprintProxyButton(const FingerprintProxyButton&) = delete;
  FingerprintProxyButton& operator=(const FingerprintProxyButton&) = delete;
  ~FingerprintProxyButton() override;

  void OnFingerprintProxyStateChanged() override;
  void OnThemeChanged() override;

 private:
  void OnButtonPressed();
  void OnBubbleClosed();
  void UpdateButton();
  std::u16string GetStateTooltip() const;

  raw_ptr<Browser> browser_;
  raw_ptr<fingerprint_browser::FingerprintProxyService> service_;
  raw_ptr<views::Widget> bubble_widget_ = nullptr;
  base::WeakPtrFactory<FingerprintProxyButton> weak_factory_{this};
};

#endif  // BRAVE_BROWSER_UI_VIEWS_TOOLBAR_FINGERPRINT_PROXY_BUTTON_H_
