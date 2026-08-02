/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_THIRD_PARTY_BLINK_RENDERER_MODULES_MEDIASTREAM_PERSONA_MEDIA_DEVICE_MAP_H_
#define BRAVE_THIRD_PARTY_BLINK_RENDERER_MODULES_MEDIASTREAM_PERSONA_MEDIA_DEVICE_MAP_H_

#include "third_party/blink/renderer/modules/modules_export.h"

namespace blink {
class ExecutionContext;
class MediaConstraints;
}  // namespace blink

namespace brave {

MODULES_EXPORT void TranslatePersonaMediaDeviceConstraints(
    blink::ExecutionContext* context,
    blink::MediaConstraints* audio,
    blink::MediaConstraints* video);

}  // namespace brave

#endif  // BRAVE_THIRD_PARTY_BLINK_RENDERER_MODULES_MEDIASTREAM_PERSONA_MEDIA_DEVICE_MAP_H_
