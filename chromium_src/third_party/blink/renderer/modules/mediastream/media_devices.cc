/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <optional>

#include "base/compiler_specific.h"
#include "brave/components/brave_shields/core/common/farbling_prng.h"
#include "brave/third_party/blink/renderer/core/farbling/brave_session_cache.h"
#include "third_party/blink/public/mojom/mediastream/media_devices.mojom-blink.h"
#include "third_party/blink/renderer/modules/mediastream/input_device_info.h"
#include "third_party/blink/renderer/modules/mediastream/media_device_info.h"

using blink::ExecutionContext;
using blink::InputDeviceInfo;
using blink::MakeGarbageCollected;
using blink::MediaDeviceInfo;
using blink::MediaDeviceInfoVector;
using blink::String;

namespace brave {

using brave_shields::FarblingPRNG;

std::optional<blink::mojom::MediaDeviceType> MediaDeviceTypeFromPersonaKind(
    const String& kind) {
  if (kind == "audioinput") {
    return blink::mojom::MediaDeviceType::kMediaAudioInput;
  }
  if (kind == "videoinput") {
    return blink::mojom::MediaDeviceType::kMediaVideoInput;
  }
  if (kind == "audiooutput") {
    return blink::mojom::MediaDeviceType::kMediaAudioOutput;
  }
  return std::nullopt;
}

bool ShouldExposeMediaDeviceLabels(const MediaDeviceInfoVector& media_devices) {
  for (const auto& media_device : media_devices) {
    if (!media_device->label().empty()) {
      return true;
    }
  }
  return false;
}

bool UsePersonaMediaDevices(ExecutionContext* context,
                            MediaDeviceInfoVector* media_devices) {
  auto persona_devices =
      BraveSessionCache::From(*context).PersonaMediaDevices();
  if (!persona_devices) {
    return false;
  }

  blink::Vector<blink::mojom::MediaDeviceType> device_types;
  device_types.ReserveInitialCapacity(persona_devices->size());
  for (const auto& persona_device : *persona_devices) {
    auto device_type = MediaDeviceTypeFromPersonaKind(persona_device.kind);
    if (!device_type) {
      return false;
    }
    device_types.push_back(*device_type);
  }

  const bool expose_labels = ShouldExposeMediaDeviceLabels(*media_devices);
  media_devices->clear();
  for (blink::wtf_size_t i = 0; i < persona_devices->size(); ++i) {
    const auto& persona_device = (*persona_devices)[i];
    const auto device_type = device_types[i];
    const String label = expose_labels ? persona_device.label : String();
    if (device_type == blink::mojom::MediaDeviceType::kMediaAudioInput ||
        device_type == blink::mojom::MediaDeviceType::kMediaVideoInput) {
      media_devices->push_back(MakeGarbageCollected<InputDeviceInfo>(
          persona_device.device_id, label, persona_device.group_id,
          device_type));
    } else {
      media_devices->push_back(MakeGarbageCollected<MediaDeviceInfo>(
          persona_device.device_id, label, persona_device.group_id,
          device_type));
    }
  }
  return true;
}

void FarbleMediaDevices(ExecutionContext* context,
                        MediaDeviceInfoVector* media_devices) {
  // |media_devices| is guaranteed not to be null here.
  if (GetBraveFarblingLevelFor(
          context, ContentSettingsType::BRAVE_WEBCOMPAT_MEDIA_DEVICES,
          BraveFarblingLevel::OFF) == BraveFarblingLevel::OFF) {
    return;
  }
  if (UsePersonaMediaDevices(context, media_devices)) {
    return;
  }
  if (media_devices->size() <= 2) {
    return;
  }
  if (media_devices->size() <= 2) {
    return;
  }
  // Shuffle the list of devices pseudo-randomly, based on the
  // domain+session key, starting with the second device.
  FarblingPRNG prng =
      BraveSessionCache::From(*context).MakePseudoRandomGenerator();
  MediaDeviceInfoVector::iterator it_begin = media_devices->begin();
  UNSAFE_TODO(std::shuffle(++it_begin, media_devices->end(), prng));
}

}  // namespace brave

#define BRAVE_MEDIA_DEVICES_DEVICES_ENUMERATED                        \
  if (ExecutionContext* context =                                     \
          ExecutionContext::From(result_tracker->GetScriptState())) { \
    brave::FarbleMediaDevices(context, &media_devices);               \
  }

#include <third_party/blink/renderer/modules/mediastream/media_devices.cc>
#undef BRAVE_MEDIA_DEVICES_DEVICES_ENUMERATED
