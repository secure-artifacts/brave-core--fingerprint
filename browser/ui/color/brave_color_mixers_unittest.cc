/* Copyright (c) 2024 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "brave/browser/ui/color/brave_color_id.h"
#include "brave/ui/color/nala/nala_color_id.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/color/chrome_color_mixers.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/color/color_mixers.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"

namespace {

double LinearChannel(uint8_t channel) {
  const double value = channel / 255.0;
  return value <= 0.04045 ? value / 12.92
                          : std::pow((value + 0.055) / 1.055, 2.4);
}

double RelativeLuminance(SkColor color) {
  return 0.2126 * LinearChannel(SkColorGetR(color)) +
         0.7152 * LinearChannel(SkColorGetG(color)) +
         0.0722 * LinearChannel(SkColorGetB(color));
}

double ContrastRatio(SkColor foreground, SkColor background) {
  const double foreground_luminance = RelativeLuminance(foreground);
  const double background_luminance = RelativeLuminance(background);
  return (std::max(foreground_luminance, background_luminance) + 0.05) /
         (std::min(foreground_luminance, background_luminance) + 0.05);
}

}  // namespace

class BraveColorMixersTest : public testing::Test {
 public:
  BraveColorMixersTest() = default;

  ui::ColorProvider& color_provider() { return color_provider_; }

  void AddColorMixers() {
    ui::AddColorMixers(&color_provider_, color_provider_key_);
    AddChromeColorMixers(&color_provider_, color_provider_key_);
  }

  void SetColorMode(ui::ColorProviderKey::ColorMode color_mode) {
    color_provider_key_.color_mode = color_mode;
  }

 private:
  ui::ColorProvider color_provider_;
  ui::ColorProviderKey color_provider_key_;
};

TEST_F(BraveColorMixersTest, ColorOverrideTest) {
  AddColorMixers();

  EXPECT_EQ(color_provider().GetColor(kColorToolbar),
            color_provider().GetColor(kColorInfoBarBackground));
  EXPECT_EQ(color_provider().GetColor(kColorOmniboxIconHover),
            SkColorSetA(color_provider().GetColor(kColorOmniboxText),
                        std::ceil(0.10f * 255.0f)));
  EXPECT_EQ(color_provider().GetColor(kColorOmniboxSecurityChipText),
            color_provider().GetColor(kColorOmniboxSecurityChipDangerous));
}

TEST_F(BraveColorMixersTest, ProvidesNalaNeutral5) {
  AddColorMixers();

  EXPECT_EQ(color_provider().GetColor(nala::kColorPrimitiveNeutral5),
            SkColorSetRGB(0x14, 0x14, 0x15));
}

TEST_F(BraveColorMixersTest, InitializesSidebarButtonColor) {
  AddColorMixers();

  EXPECT_NE(color_provider().GetColor(kColorSidebarButtonBase),
            SkColorSetRGB(0xff, 0x00, 0x00));
}

TEST_F(BraveColorMixersTest, UsesNalaComboboxOutline) {
  AddColorMixers();

  EXPECT_EQ(color_provider().GetColor(ui::kColorComboboxContainerOutline),
            SkColorSetRGB(0x78, 0x78, 0x7c));
}

TEST_F(BraveColorMixersTest, UsesNalaComboboxOutlineInDarkMode) {
  SetColorMode(ui::ColorProviderKey::ColorMode::kDark);
  AddColorMixers();

  EXPECT_EQ(color_provider().GetColor(ui::kColorComboboxContainerOutline),
            SkColorSetRGB(0x90, 0x90, 0x93));
}

TEST_F(BraveColorMixersTest, UsesAccessibleSecondaryForeground) {
  AddColorMixers();

  const SkColor foreground =
      color_provider().GetColor(ui::kColorSecondaryForeground);
  EXPECT_EQ(foreground, SkColorSetRGB(0x5e, 0x5e, 0x62));
  EXPECT_GE(ContrastRatio(foreground, SK_ColorWHITE), 4.5);
}

TEST_F(BraveColorMixersTest, UsesAccessibleSecondaryForegroundInDarkMode) {
  SetColorMode(ui::ColorProviderKey::ColorMode::kDark);
  AddColorMixers();

  const SkColor foreground =
      color_provider().GetColor(ui::kColorSecondaryForeground);
  EXPECT_EQ(foreground, SkColorSetRGB(0xaa, 0xaa, 0xad));
  EXPECT_GE(ContrastRatio(foreground, SkColorSetRGB(0x20, 0x20, 0x20)), 4.5);
}
