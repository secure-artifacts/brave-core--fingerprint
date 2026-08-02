/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "third_party/blink/renderer/modules/gamepad/navigator_gamepad.h"

#include "brave/third_party/blink/renderer/core/farbling/brave_session_cache.h"

namespace blink {

namespace {

bool ShouldUsePersonaGamepad(ExecutionContext* context) {
  return context &&
         brave::GetBraveFarblingLevelFor(
             context, ContentSettingsType::BRAVE_WEBCOMPAT_NONE,
             BraveFarblingLevel::OFF) != BraveFarblingLevel::OFF &&
         brave::BraveSessionCache::From(*context).HasPersonaL1();
}

void SanitizePersonaGamepads(ExecutionContext* context,
                             HeapVector<Member<Gamepad>>& gamepads) {
  if (!ShouldUsePersonaGamepad(context)) {
    return;
  }
  for (auto& gamepad : gamepads) {
    if (gamepad) {
      gamepad->SetId("Standard Gamepad");
    }
  }
}

}  // namespace

}  // namespace blink

#define BRAVE_NAVIGATOR_GAMEPAD_DID_SAMPLE(gamepads) \
  blink::SanitizePersonaGamepads(GetExecutionContext(), gamepads);

#include <third_party/blink/renderer/modules/gamepad/navigator_gamepad.cc>

#undef BRAVE_NAVIGATOR_GAMEPAD_DID_SAMPLE
