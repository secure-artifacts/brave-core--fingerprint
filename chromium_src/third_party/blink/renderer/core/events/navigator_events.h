/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_CHROMIUM_SRC_THIRD_PARTY_BLINK_RENDERER_CORE_EVENTS_NAVIGATOR_EVENTS_H_
#define BRAVE_CHROMIUM_SRC_THIRD_PARTY_BLINK_RENDERER_CORE_EVENTS_NAVIGATOR_EVENTS_H_

#define maxTouchPoints(...)                 \
  maxTouchPoints_ChromiumImpl(__VA_ARGS__); \
  static int32_t maxTouchPoints(__VA_ARGS__)

#include <third_party/blink/renderer/core/events/navigator_events.h>  // IWYU pragma: export

#undef maxTouchPoints

#endif  // BRAVE_CHROMIUM_SRC_THIRD_PARTY_BLINK_RENDERER_CORE_EVENTS_NAVIGATOR_EVENTS_H_
