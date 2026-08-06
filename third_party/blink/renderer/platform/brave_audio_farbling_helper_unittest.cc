/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/third_party/blink/renderer/platform/brave_audio_farbling_helper.h"

#include <array>
#include <bit>
#include <cstdint>
#include <limits>

#include "base/containers/span.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {
namespace {

constexpr double kFudgeFactor = 0.999999;

TEST(BraveAudioFarblingHelperTest, PersonaTokenSeedControlsOutput) {
  constexpr std::array<float, 8> kInput = {
      -0.875f, -0.625f, -0.375f, -0.125f, 0.125f, 0.375f, 0.625f, 0.875f,
  };
  BraveAudioFarblingHelper first(kFudgeFactor, 0x12345678, false);
  BraveAudioFarblingHelper same(kFudgeFactor, 0x12345678, false);
  BraveAudioFarblingHelper different(kFudgeFactor, 0x87654321, false);
  auto first_output = kInput;
  auto same_output = kInput;
  auto different_output = kInput;

  first.FarbleAudioChannelForPersona(base::span(first_output), 0);
  same.FarbleAudioChannelForPersona(base::span(same_output), 0);
  different.FarbleAudioChannelForPersona(base::span(different_output), 0);

  EXPECT_EQ(first_output, same_output);
  EXPECT_NE(first_output, different_output);
}

TEST(BraveAudioFarblingHelperTest, PersonaTransformIsIdempotent) {
  BraveAudioFarblingHelper helper(kFudgeFactor, 0x12345678, false);
  std::array<float, 4> samples = {-0.75f, -0.25f, 0.25f, 0.75f};

  helper.FarbleAudioChannelForPersona(base::span(samples), 17);
  const auto once = samples;
  helper.FarbleAudioChannelForPersona(base::span(samples), 17);

  EXPECT_EQ(once, samples);
}

TEST(BraveAudioFarblingHelperTest, PersonaTransformIsConsistentAcrossSlices) {
  constexpr std::array<float, 8> kInput = {
      -0.875f, -0.625f, -0.375f, -0.125f, 0.125f, 0.375f, 0.625f, 0.875f,
  };
  BraveAudioFarblingHelper helper(kFudgeFactor, 0x12345678, false);
  auto full = kInput;
  std::array<float, 3> slice = {kInput[3], kInput[4], kInput[5]};

  helper.FarbleAudioChannelForPersona(base::span(full), 0);
  helper.FarbleAudioChannelForPersona(base::span(slice), 3);

  EXPECT_EQ(full[3], slice[0]);
  EXPECT_EQ(full[4], slice[1]);
  EXPECT_EQ(full[5], slice[2]);
}

TEST(BraveAudioFarblingHelperTest, EqualSamplesStayEqualAcrossPositions) {
  BraveAudioFarblingHelper helper(kFudgeFactor, 0x12345678, false);
  std::array<float, 3> samples = {0.31415927f, 0.31415927f, 0.31415927f};

  helper.FarbleAudioChannelForPersona(base::span(samples), 275);

  EXPECT_EQ(samples[0], samples[1]);
  EXPECT_EQ(samples[1], samples[2]);
}

TEST(BraveAudioFarblingHelperTest, PersonaTransformNormalizesLowBitNoise) {
  BraveAudioFarblingHelper helper(kFudgeFactor, 0x12345678, false);
  std::array<float, 1> first = {std::bit_cast<float>(uint32_t{0x3f000001})};
  std::array<float, 1> second = {std::bit_cast<float>(uint32_t{0x3f0000fe})};

  helper.FarbleAudioChannelForPersona(base::span(first), 31);
  helper.FarbleAudioChannelForPersona(base::span(second), 31);

  EXPECT_EQ(std::bit_cast<uint32_t>(first[0]),
            std::bit_cast<uint32_t>(second[0]));
}

TEST(BraveAudioFarblingHelperTest, QuantizedSamplesKeepDistinctMarkers) {
  BraveAudioFarblingHelper helper(kFudgeFactor, 0x12345678, false);
  std::array<float, 2> samples = {
      std::bit_cast<float>(uint32_t{0x3f000001}),
      std::bit_cast<float>(uint32_t{0x3f000101}),
  };

  helper.FarbleAudioChannelForPersona(base::span(samples), 0);

  EXPECT_NE(std::bit_cast<uint32_t>(samples[0]) & 0xffu,
            std::bit_cast<uint32_t>(samples[1]) & 0xffu);
}

TEST(BraveAudioFarblingHelperTest, PersonaTransformPreservesSpecialValues) {
  const float nan = std::bit_cast<float>(uint32_t{0x7fc12345});
  std::array<float, 5> samples = {0.0f, -0.0f, nan,
                                  std::numeric_limits<float>::infinity(),
                                  -std::numeric_limits<float>::infinity()};
  const auto original = samples;
  BraveAudioFarblingHelper helper(kFudgeFactor, 0x12345678, false);

  helper.FarbleAudioChannelForPersona(base::span(samples), 0);

  for (size_t i = 0; i < samples.size(); ++i) {
    EXPECT_EQ(std::bit_cast<uint32_t>(original[i]),
              std::bit_cast<uint32_t>(samples[i]));
  }
}

}  // namespace
}  // namespace blink
