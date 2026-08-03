/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/views/toolbar/fingerprint_proxy_button.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/i18n/time_formatting.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "brave/browser/fingerprint_browser/fingerprint_proxy_service_factory.h"
#include "brave/browser/fingerprint_browser/fingerprint_proxy_ui_strings.h"
#include "brave/browser/ui/brave_pages.h"
#include "brave/grit/brave_generated_resources.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "components/grit/brave_components_resources.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/color/color_id.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"

constexpr std::string_view kIsoCountryCodes =
    "ad ae af ag ai al am ao aq ar as at au aw ax az ba bb bd be bf bg bh bi "
    "bj bl bm bn bo bq br bs bt bv bw by bz ca cc cd cf cg ch ci ck cl cm cn "
    "co cr cu cv cw cx cy cz de dj dk dm do dz ec ee eg eh er es et fi fj fk "
    "fm fo fr ga gb gd ge gf gg gh gi gl gm gn gp gq gr gs gt gu gw gy hk hm "
    "hn hr ht hu id ie il im in io iq ir is it je jm jo jp ke kg kh ki km kn "
    "kp kr kw ky kz la lb lc li lk lr ls lt lu lv ly ma mc md me mf mg mh mk "
    "ml mm mn mo mp mq mr ms mt mu mv mw mx my mz na nc ne nf ng ni nl no np "
    "nr nu nz om pa pe pf pg ph pk pl pm pn pr ps pt pw py qa re ro rs ru rw "
    "sa sb sc sd se sg sh si sj sk sl sm sn so sr ss st sv sx sy sz tc td tf "
    "tg th tj tk tl tm tn to tr tt tv tw tz ua ug um us uy uz va vc ve vg vi "
    "vn vu wf ws ye yt za zm zw";

constexpr int kFlagAtlasColumns = 16;
constexpr int kFlagCellWidth = 64;
constexpr int kFlagCellHeight = 48;

std::optional<size_t> CountryFlagIndex(std::string_view country_code) {
  const std::string normalized = base::ToLowerASCII(country_code);
  if (normalized.size() != 2) {
    return std::nullopt;
  }
  const size_t position = kIsoCountryCodes.find(normalized);
  if (position == std::string_view::npos || position % 3 != 0) {
    return std::nullopt;
  }
  return position / 3;
}

gfx::ImageSkia CountryFlagImage(std::string_view country_code,
                                const gfx::Size& target_size) {
  const std::optional<size_t> index = CountryFlagIndex(country_code);
  if (!index) {
    return gfx::ImageSkia();
  }

  const SkBitmap atlas = ui::ResourceBundle::GetSharedInstance()
                             .GetImageNamed(IDR_FINGERPRINT_PROXY_FLAG_ATLAS)
                             .AsBitmap();
  gfx::ImageSkia atlas_image;
  atlas_image.AddRepresentation(gfx::ImageSkiaRep(atlas, 1.0f));
  const gfx::Rect bounds(
      static_cast<int>(*index % kFlagAtlasColumns) * kFlagCellWidth,
      static_cast<int>(*index / kFlagAtlasColumns) * kFlagCellHeight,
      kFlagCellWidth, kFlagCellHeight);
  const gfx::ImageSkia flag =
      gfx::ImageSkiaOperations::ExtractSubset(atlas_image, bounds);
  return gfx::ImageSkiaOperations::CreateResizedImage(
      flag, skia::ImageOperations::RESIZE_BEST, target_size);
}

class FingerprintProxyBubble
    : public views::BubbleDialogDelegateView,
      public fingerprint_browser::FingerprintProxyService::Observer {
 public:
  FingerprintProxyBubble(views::View* anchor_view,
                         Browser* browser,
                         fingerprint_browser::FingerprintProxyService* service,
                         base::OnceClosure on_close)
      : BubbleDialogDelegateView(anchor_view,
                                 views::BubbleBorder::Arrow::TOP_RIGHT),
        browser_(browser),
        service_(service),
        on_close_(std::move(on_close)) {
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
    SetTitle(
        l10n_util::GetStringUTF16(IDS_FINGERPRINT_PROFILE_PROXY_BUBBLE_TITLE));
    set_fixed_width(320);
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(16), 12));

    status_label_ = AddChildView(std::make_unique<views::Label>());
    status_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    status_label_->SetMultiLine(true);

    detail_label_ = AddChildView(std::make_unique<views::Label>());
    detail_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    detail_label_->SetMultiLine(true);
    detail_label_->SetEnabledColor(ui::kColorLabelForegroundSecondary);

    auto* primary_actions = AddChildView(std::make_unique<views::View>());
    primary_actions->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
    configure_button_ =
        primary_actions->AddChildView(std::make_unique<views::MdTextButton>(
            base::BindRepeating(&FingerprintProxyBubble::OpenSettings,
                                base::Unretained(this)),
            l10n_util::GetStringUTF16(
                IDS_FINGERPRINT_PROFILE_PROXY_CONFIGURE)));
    primary_actions->AddChildView(std::make_unique<views::MdTextButton>(
        base::BindRepeating(&FingerprintProxyBubble::OpenGuide,
                            base::Unretained(this)),
        l10n_util::GetStringUTF16(IDS_SHOW_FINGERPRINT_GUIDE)));

    auto* state_actions = AddChildView(std::make_unique<views::View>());
    state_actions->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
    revalidate_button_ =
        state_actions->AddChildView(std::make_unique<views::MdTextButton>(
            base::BindRepeating(&FingerprintProxyBubble::Revalidate,
                                base::Unretained(this)),
            l10n_util::GetStringUTF16(
                IDS_FINGERPRINT_PROFILE_PROXY_REVALIDATE)));
    disable_button_ =
        state_actions->AddChildView(std::make_unique<views::MdTextButton>(
            base::BindRepeating(&FingerprintProxyBubble::Disable,
                                base::Unretained(this)),
            l10n_util::GetStringUTF16(IDS_FINGERPRINT_PROFILE_PROXY_DISABLE)));

    service_->AddObserver(this);
    Update();
  }

  FingerprintProxyBubble(const FingerprintProxyBubble&) = delete;
  FingerprintProxyBubble& operator=(const FingerprintProxyBubble&) = delete;

  ~FingerprintProxyBubble() override {
    service_->RemoveObserver(this);
    if (on_close_) {
      std::move(on_close_).Run();
    }
  }

  void OnFingerprintProxyStateChanged() override { Update(); }

 private:
  void Update() {
    const fingerprint_browser::FingerprintProxyState state =
        service_->GetState();
    std::u16string status = fingerprint_browser::GetProxyUiMessage(
        state.status_code, state.net_error);
    if (status.empty() &&
        state.state == fingerprint_browser::kProxyStateUnconfigured) {
      status = l10n_util::GetStringUTF16(
          IDS_SETTINGS_FINGERPRINT_PROFILE_PROXY_NO_PROXY_RISK_DESC);
    }
    if (state.warning_code != fingerprint_browser::kProxyWarningNone) {
      status = base::StrCat(
          {status, u"\n",
           fingerprint_browser::GetProxyUiMessage(state.warning_code)});
    }
    status_label_->SetText(status);
    if (GetColorProvider()) {
      const bool error = state.state == fingerprint_browser::kProxyStateError;
      const bool warning =
          state.state == fingerprint_browser::kProxyStateUnconfigured ||
          state.state == fingerprint_browser::kProxyStateStale ||
          state.state == fingerprint_browser::kProxyStateConflict ||
          state.warning_code != fingerprint_browser::kProxyWarningNone;
      status_label_->SetEnabledColor(GetColorProvider()->GetColor(
          error     ? ui::kColorAlertHighSeverity
          : warning ? ui::kColorAlertMediumSeverityIcon
                    : ui::kColorLabelForeground));
    }

    std::vector<std::u16string> details;
    if (!state.egress_ip.empty()) {
      details.push_back(base::UTF8ToUTF16(state.egress_ip));
    }
    if (state.geo) {
      details.push_back(base::UTF8ToUTF16(
          base::StrCat({fingerprint_browser::GetChineseCountryName(
                            state.geo->country_code, state.geo->country_name),
                        ", ", state.geo->city_name})));
      details.push_back(base::UTF8ToUTF16(state.geo->timezone));
    }
    if (!state.last_verified.is_null()) {
      details.push_back(base::TimeFormatShortDateAndTime(state.last_verified));
    }
    detail_label_->SetText(base::JoinString(details, u"\n"));

    revalidate_button_->SetVisible(state.enabled);
    disable_button_->SetVisible(state.enabled);
    revalidate_button_->SetEnabled(state.state !=
                                   fingerprint_browser::kProxyStateVerifying);
    InvalidateLayout();
  }

  void OpenSettings() {
    chrome::ShowSettingsSubPageForProfile(browser_->profile(),
                                          "fingerprintProfileProxy");
    GetWidget()->Close();
  }

  void OpenGuide() {
    brave::ShowFingerprintGuide(browser_);
    GetWidget()->Close();
  }

  void Revalidate() {
    revalidate_button_->SetEnabled(false);
    service_->Revalidate(
        base::BindOnce([](fingerprint_browser::ProxyVerificationResult) {}));
  }

  void Disable() {
    service_->Disable(base::BindOnce([]() {}));
  }

  raw_ptr<Browser> browser_;
  raw_ptr<fingerprint_browser::FingerprintProxyService> service_;
  raw_ptr<views::Label> status_label_;
  raw_ptr<views::Label> detail_label_;
  raw_ptr<views::MdTextButton> configure_button_;
  raw_ptr<views::MdTextButton> revalidate_button_;
  raw_ptr<views::MdTextButton> disable_button_;
  base::OnceClosure on_close_;
};

FingerprintProxyButton::FingerprintProxyButton(Browser* browser)
    : ToolbarButton(
          base::BindRepeating(&FingerprintProxyButton::OnButtonPressed,
                              base::Unretained(this))),
      browser_(browser),
      service_(
          fingerprint_browser::FingerprintProxyServiceFactory::GetForProfile(
              browser->profile())) {
  CHECK(service_);
  service_->AddObserver(this);
  SetAccessibleName(
      l10n_util::GetStringUTF16(IDS_FINGERPRINT_PROFILE_PROXY_TOOLBAR_NAME));
  UpdateButton();
}

FingerprintProxyButton::~FingerprintProxyButton() {
  weak_factory_.InvalidateWeakPtrs();
  if (bubble_widget_) {
    bubble_widget_->CloseNow();
    bubble_widget_ = nullptr;
  }
  service_->RemoveObserver(this);
}

void FingerprintProxyButton::OnFingerprintProxyStateChanged() {
  UpdateButton();
}

void FingerprintProxyButton::OnThemeChanged() {
  ToolbarButton::OnThemeChanged();
  UpdateButton();
}

void FingerprintProxyButton::OnButtonPressed() {
  if (bubble_widget_) {
    bubble_widget_->Close();
    return;
  }

  auto* bubble = new FingerprintProxyBubble(
      this, browser_, service_,
      base::BindOnce(&FingerprintProxyButton::OnBubbleClosed,
                     weak_factory_.GetWeakPtr()));
  bubble_widget_ = views::BubbleDialogDelegateView::CreateBubble(bubble);
  bubble_widget_->Show();
}

void FingerprintProxyButton::OnBubbleClosed() {
  bubble_widget_ = nullptr;
}

void FingerprintProxyButton::UpdateButton() {
  const fingerprint_browser::FingerprintProxyState state = service_->GetState();
  SetTooltipText(GetStateTooltip());
  SetText(std::u16string());

  if ((state.state == fingerprint_browser::kProxyStateActive ||
       state.state == fingerprint_browser::kProxyStateStale) &&
      state.geo) {
    gfx::ImageSkia flag =
        CountryFlagImage(state.geo->country_code, gfx::Size(20, 15));
    if (!flag.isNull()) {
      if ((state.state == fingerprint_browser::kProxyStateStale ||
           state.warning_code != fingerprint_browser::kProxyWarningNone) &&
          GetColorProvider()) {
        const gfx::ImageSkia badge = gfx::CreateVectorIcon(
            vector_icons::kWarningIcon, 8,
            GetColorProvider()->GetColor(ui::kColorAlertMediumSeverityIcon));
        flag = gfx::ImageSkiaOperations::CreateIconWithBadge(flag, badge);
      }
      SetImageModel(views::Button::STATE_NORMAL,
                    ui::ImageModel::FromImageSkia(flag));
      return;
    }
    SetImageModel(views::Button::STATE_NORMAL, ui::ImageModel());
    SetText(base::UTF8ToUTF16(base::ToUpperASCII(state.geo->country_code)));
    return;
  }

  const gfx::VectorIcon* icon = &vector_icons::kGlobeIcon;
  ui::ColorId color = kColorToolbarButtonIcon;
  if (state.state == fingerprint_browser::kProxyStateUnconfigured) {
    if (GetColorProvider()) {
      const gfx::ImageSkia globe = gfx::CreateVectorIcon(
          vector_icons::kGlobeIcon, GetIconSize(),
          GetColorProvider()->GetColor(kColorToolbarButtonIcon));
      const gfx::ImageSkia badge = gfx::CreateVectorIcon(
          vector_icons::kWarningIcon, 8,
          GetColorProvider()->GetColor(ui::kColorAlertMediumSeverityIcon));
      SetImageModel(
          views::Button::STATE_NORMAL,
          ui::ImageModel::FromImageSkia(
              gfx::ImageSkiaOperations::CreateIconWithBadge(globe, badge)));
      return;
    }
    color = ui::kColorAlertMediumSeverityIcon;
  } else if (state.state == fingerprint_browser::kProxyStateVerifying) {
    icon = &vector_icons::kSyncIcon;
  } else if (state.state == fingerprint_browser::kProxyStateError) {
    icon = &vector_icons::kErrorIcon;
    color = ui::kColorAlertHighSeverity;
  } else if (state.state == fingerprint_browser::kProxyStateConflict) {
    icon = &vector_icons::kInfoIcon;
    color = ui::kColorAlertMediumSeverityIcon;
  }
  SetImageModel(views::Button::STATE_NORMAL,
                ui::ImageModel::FromVectorIcon(*icon, color, GetIconSize()));
}

std::u16string FingerprintProxyButton::GetStateTooltip() const {
  const fingerprint_browser::FingerprintProxyState state = service_->GetState();
  const std::u16string name =
      l10n_util::GetStringUTF16(IDS_FINGERPRINT_PROFILE_PROXY_TOOLBAR_NAME);
  const std::u16string message = fingerprint_browser::GetProxyUiMessage(
      state.status_code, state.net_error);
  if (message.empty()) {
    if (state.state == fingerprint_browser::kProxyStateUnconfigured) {
      return base::StrCat(
          {name, u": ",
           l10n_util::GetStringUTF16(
               IDS_SETTINGS_FINGERPRINT_PROFILE_PROXY_NO_PROXY_RISK_TITLE)});
    }
    return name;
  }
  std::u16string tooltip = base::StrCat({name, u": ", message});
  if (state.warning_code != fingerprint_browser::kProxyWarningNone) {
    base::StrAppend(
        &tooltip,
        {u" ", fingerprint_browser::GetProxyUiMessage(state.warning_code)});
  }
  return tooltip;
}

BEGIN_METADATA(FingerprintProxyButton) END_METADATA
