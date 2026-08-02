/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/third_party/blink/renderer/modules/mediastream/persona_media_device_map.h"

#define BRAVE_USER_MEDIA_REQUEST_TRANSLATE_PERSONA_DEVICE_IDS(context, audio, \
                                                              video)          \
  brave::TranslatePersonaMediaDeviceConstraints(context, audio, video);

#include <third_party/blink/renderer/modules/mediastream/user_media_request.cc>

#undef BRAVE_USER_MEDIA_REQUEST_TRANSLATE_PERSONA_DEVICE_IDS
