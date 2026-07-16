/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_CHROMIUM_SRC_THIRD_PARTY_BLINK_RENDERER_MODULES_GAMEPAD_NAVIGATOR_GAMEPAD_H_
#define BRAVE_CHROMIUM_SRC_THIRD_PARTY_BLINK_RENDERER_MODULES_GAMEPAD_NAVIGATOR_GAMEPAD_H_

#define getGamepads(...)                 \
  getGamepads_ChromiumImpl(__VA_ARGS__); \
  static HeapVector<Member<Gamepad>> getGamepads(__VA_ARGS__)

#define SampleAndCompareGamepadState()         \
  SampleAndCompareGamepadState_ChromiumImpl(); \
  void SampleAndCompareGamepadState()

#include <third_party/blink/renderer/modules/gamepad/navigator_gamepad.h>  // IWYU pragma: export

#undef SampleAndCompareGamepadState
#undef getGamepads

#endif  // BRAVE_CHROMIUM_SRC_THIRD_PARTY_BLINK_RENDERER_MODULES_GAMEPAD_NAVIGATOR_GAMEPAD_H_
