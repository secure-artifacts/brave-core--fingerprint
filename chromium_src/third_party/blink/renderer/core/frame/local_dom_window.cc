/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "third_party/blink/renderer/core/frame/local_dom_window.h"

#include "brave/third_party/blink/renderer/core/farbling/brave_session_cache.h"
// Include mouse_event.h, pointer_event.h here to avoid re-defining
// tokens named screenX, screenY:
#include "third_party/blink/renderer/core/events/mouse_event.h"
#include "third_party/blink/renderer/core/events/pointer_event.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"

#define outerHeight outerHeight_ChromiumImpl
#define outerWidth outerWidth_ChromiumImpl
#define screenX screenX_ChromiumImpl
#define screenY screenY_ChromiumImpl
#define devicePixelRatio devicePixelRatio_ChromiumImpl
#define resizeTo resizeTo_ChromiumImpl
#define moveTo moveTo_ChromiumImpl

#define BRAVE_LOCAL_DOM_WINDOW_CAN_EXECUTE_SCRIPTS                 \
  if (WebContentSettingsClient* settings_client =                  \
          GetFrame()->GetContentSettingsClient()) {                \
    script_enabled = settings_client->AllowScript(script_enabled); \
  }

// This translation unit has a call to `ScriptEnabled` that is not passing a
// URL, and this override is necessary to correct that call.
#define ScriptEnabled() ScriptEnabled(Url())

#include <third_party/blink/renderer/core/frame/local_dom_window.cc>
#undef BRAVE_LOCAL_DOM_WINDOW_CAN_EXECUTE_SCRIPTS
#undef outerHeight
#undef outerWidth
#undef screenX
#undef screenY
#undef devicePixelRatio
#undef resizeTo
#undef moveTo
#undef ScriptEnabled

namespace blink {

using brave::BlockScreenFingerprinting;
using brave::FarbleInteger;
using brave::FarbleKey;

const SecurityOrigin* GetEphemeralStorageOrigin(LocalDOMWindow* window) {
  auto* frame = window->GetFrame();
  if (!frame) {
    return nullptr;
  }

  if (auto* settings_client = frame->GetContentSettingsClient()) {
    return settings_client->GetEphemeralStorageOriginSync().Get();
  }

  return nullptr;
}

int LocalDOMWindow::outerWidth() const {
  // Prevent fingerprinter use of outerWidth by returning a farbled value near
  // innerWidth instead:
  ExecutionContext* context = GetExecutionContext();
  auto* top_window = DynamicTo<LocalDOMWindow>(top());
  return BlockScreenFingerprinting(context) && top_window
             ? FarbleInteger(context, brave::FarbleKey::kWindowInnerWidth,
                             top_window->innerWidth(), 0, 8)
             : outerWidth_ChromiumImpl();
}

int LocalDOMWindow::outerHeight() const {
  // Prevent fingerprinter use of outerHeight by returning a farbled value near
  // innerHeight instead:
  ExecutionContext* context = GetExecutionContext();
  auto* top_window = DynamicTo<LocalDOMWindow>(top());
  return BlockScreenFingerprinting(context) && top_window
             ? FarbleInteger(context, brave::FarbleKey::kWindowInnerHeight,
                             top_window->innerHeight(), 0, 8)
             : outerHeight_ChromiumImpl();
}

int LocalDOMWindow::screenX() const {
  ExecutionContext* context = GetExecutionContext();
  if (!BlockScreenFingerprinting(context)) {
    return screenX_ChromiumImpl();
  }
  if (auto value = brave::BraveSessionCache::From(*context).PersonaWindowX()) {
    return *value;
  }
  return FarbleInteger(context, brave::FarbleKey::kWindowScreenX, 0, 0, 8);
}

int LocalDOMWindow::screenY() const {
  ExecutionContext* context = GetExecutionContext();
  if (!BlockScreenFingerprinting(context)) {
    return screenY_ChromiumImpl();
  }
  if (auto value = brave::BraveSessionCache::From(*context).PersonaWindowY()) {
    return *value;
  }
  return FarbleInteger(context, brave::FarbleKey::kWindowScreenY, 0, 0, 8);
}

double LocalDOMWindow::devicePixelRatio() const {
  ExecutionContext* context = GetExecutionContext();
  if (BlockScreenFingerprinting(context)) {
    if (auto value = brave::BraveSessionCache::From(*context)
                         .PersonaScreenDeviceScaleFactor()) {
      return *value;
    }
  }
  return devicePixelRatio_ChromiumImpl();
}

void LocalDOMWindow::resizeTo(int width,
                              int height,
                              ExceptionState& exception_state) const {
  ExecutionContext* context = GetExecutionContext();
  if (BlockScreenFingerprinting(context)) {
    resizeTo_ChromiumImpl(width + outerWidth_ChromiumImpl() - outerWidth(),
                          height + outerHeight_ChromiumImpl() - outerHeight(),
                          exception_state);
  } else {
    resizeTo_ChromiumImpl(width, height, exception_state);
  }
}

void LocalDOMWindow::moveTo(int x, int y) const {
  ExecutionContext* context = GetExecutionContext();
  if (BlockScreenFingerprinting(context)) {
    moveTo_ChromiumImpl(x + screenX_ChromiumImpl() - screenX(),
                        y + screenY_ChromiumImpl() - screenY());
  } else {
    moveTo_ChromiumImpl(x, y);
  }
}

}  // namespace blink
