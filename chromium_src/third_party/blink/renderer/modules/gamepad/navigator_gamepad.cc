/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "third_party/blink/renderer/modules/gamepad/navigator_gamepad.h"

#include "brave/third_party/blink/renderer/core/farbling/brave_session_cache.h"

#define getGamepads getGamepads_ChromiumImpl
#define SampleAndCompareGamepadState SampleAndCompareGamepadState_ChromiumImpl
#include <third_party/blink/renderer/modules/gamepad/navigator_gamepad.cc>
#undef SampleAndCompareGamepadState
#undef getGamepads

namespace blink {

namespace {

bool ShouldUsePersonaGamepad(ExecutionContext* context) {
  return context &&
         brave::GetBraveFarblingLevelFor(
             context, ContentSettingsType::BRAVE_WEBCOMPAT_NONE,
             BraveFarblingLevel::OFF) != BraveFarblingLevel::OFF &&
         brave::BraveSessionCache::From(*context).HasPersonaL1();
}

HeapVector<Member<Gamepad>> EmptyGamepads() {
  HeapVector<Member<Gamepad>> result;
  result.resize(device::Gamepads::kItemsLengthCap);
  return result;
}

}  // namespace

HeapVector<Member<Gamepad>> NavigatorGamepad::getGamepads(
    Navigator& navigator,
    ExceptionState& exception_state) {
  LocalDOMWindow* window = navigator.DomWindow();
  if (!ShouldUsePersonaGamepad(window)) {
    return getGamepads_ChromiumImpl(navigator, exception_state);
  }

  ExecutionContext* context = window;
  if (!context->IsFeatureEnabled(
          network::mojom::PermissionsPolicyFeature::kGamepad)) {
    exception_state.ThrowSecurityError(kFeaturePolicyBlocked);
    return EmptyGamepads();
  }

  return EmptyGamepads();
}

void NavigatorGamepad::SampleAndCompareGamepadState() {
  if (!ShouldUsePersonaGamepad(GetExecutionContext())) {
    SampleAndCompareGamepadState_ChromiumImpl();
    return;
  }

  gamepads_.clear();
  gamepads_.resize(device::Gamepads::kItemsLengthCap);
  gamepads_back_.clear();
}

}  // namespace blink
