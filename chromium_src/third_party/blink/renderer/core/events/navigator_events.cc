/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "third_party/blink/renderer/core/events/navigator_events.h"

#include "brave/third_party/blink/renderer/core/farbling/brave_session_cache.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/navigator.h"

#define maxTouchPoints maxTouchPoints_ChromiumImpl
#include <third_party/blink/renderer/core/events/navigator_events.cc>
#undef maxTouchPoints

namespace blink {

int32_t NavigatorEvents::maxTouchPoints(Navigator& navigator) {
  LocalDOMWindow* window = navigator.DomWindow();
  ExecutionContext* context = window ? window->GetExecutionContext() : nullptr;
  if (brave::GetBraveFarblingLevelFor(
          context, ContentSettingsType::BRAVE_WEBCOMPAT_NONE,
          BraveFarblingLevel::OFF) != BraveFarblingLevel::OFF) {
    if (auto persona_value =
            brave::BraveSessionCache::From(*context).PersonaMaxTouchPoints()) {
      return *persona_value;
    }
  }
  return maxTouchPoints_ChromiumImpl(navigator);
}

}  // namespace blink
