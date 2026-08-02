/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_CHROMIUM_SRC_THIRD_PARTY_BLINK_RENDERER_MODULES_WEBGL_WEBGL2_RENDERING_CONTEXT_BASE_H_
#define BRAVE_CHROMIUM_SRC_THIRD_PARTY_BLINK_RENDERER_MODULES_WEBGL_WEBGL2_RENDERING_CONTEXT_BASE_H_

#include "third_party/blink/renderer/modules/webgl/webgl_extension.h"
#include "third_party/blink/renderer/modules/webgl/webgl_rendering_context_base.h"

#define readPixels(...)                 \
  readPixels_ChromiumImpl(__VA_ARGS__); \
  void readPixels(__VA_ARGS__)

#include <third_party/blink/renderer/modules/webgl/webgl2_rendering_context_base.h>  // IWYU pragma: export

#undef readPixels

#endif  // BRAVE_CHROMIUM_SRC_THIRD_PARTY_BLINK_RENDERER_MODULES_WEBGL_WEBGL2_RENDERING_CONTEXT_BASE_H_
