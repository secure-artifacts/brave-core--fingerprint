/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>
#include <optional>

#include "base/compiler_specific.h"
#include "brave/components/brave_shields/core/common/farbling_prng.h"
#include "brave/third_party/blink/renderer/core/farbling/brave_session_cache.h"
#include "brave/third_party/blink/renderer/modules/mediastream/persona_media_device_map.h"
#include "third_party/blink/public/mojom/mediastream/media_devices.mojom-blink.h"
#include "third_party/blink/renderer/modules/mediastream/input_device_info.h"
#include "third_party/blink/renderer/modules/mediastream/media_constraints.h"
#include "third_party/blink/renderer/modules/mediastream/media_device_info.h"

using blink::ExecutionContext;
using blink::InputDeviceInfo;
using blink::MakeGarbageCollected;
using blink::MediaDeviceInfo;
using blink::MediaDeviceInfoVector;
using blink::String;

namespace brave {

using brave_shields::FarblingPRNG;

class PersonaMediaDeviceMap final
    : public blink::GarbageCollected<PersonaMediaDeviceMap>,
      public blink::Supplement<ExecutionContext> {
 public:
  static const char kSupplementName[];

  explicit PersonaMediaDeviceMap(ExecutionContext& context)
      : Supplement<ExecutionContext>(context) {}

  static PersonaMediaDeviceMap& From(ExecutionContext& context) {
    auto* supplement =
        Supplement<ExecutionContext>::From<PersonaMediaDeviceMap>(context);
    if (!supplement) {
      supplement = blink::MakeGarbageCollected<PersonaMediaDeviceMap>(context);
      Supplement<ExecutionContext>::ProvideTo(context, supplement);
    }
    return *supplement;
  }

  void Reset() {
    device_ids_.clear();
    group_ids_.clear();
  }

  void Add(const String& persona_device_id,
           const String& real_device_id,
           const String& persona_group_id,
           const String& real_group_id,
           blink::mojom::MediaDeviceType device_type) {
    device_ids_.Set(persona_device_id, real_device_id);
    if (device_type == blink::mojom::MediaDeviceType::kMediaAudioInput ||
        device_type == blink::mojom::MediaDeviceType::kMediaVideoInput) {
      group_ids_.Set(persona_group_id, real_group_id);
    }
  }

  String RealDeviceId(const String& value) const {
    const auto it = device_ids_.find(value);
    return it == device_ids_.end() ? value : it->value;
  }

  String RealGroupId(const String& value) const {
    const auto it = group_ids_.find(value);
    return it == group_ids_.end() ? value : it->value;
  }

  void Trace(blink::Visitor* visitor) const override {
    Supplement<ExecutionContext>::Trace(visitor);
  }

 private:
  blink::HashMap<String, String> device_ids_;
  blink::HashMap<String, String> group_ids_;
};

const char PersonaMediaDeviceMap::kSupplementName[] = "PersonaMediaDeviceMap";

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
  PersonaMediaDeviceMap& mappings = PersonaMediaDeviceMap::From(*context);
  mappings.Reset();
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
  MediaDeviceInfoVector persona_results;
  for (blink::wtf_size_t i = 0; i < persona_devices->size(); ++i) {
    const auto& persona_device = (*persona_devices)[i];
    const auto device_type = device_types[i];
    MediaDeviceInfo* real_device = nullptr;
    for (const auto& candidate : *media_devices) {
      if (candidate->DeviceType() == device_type) {
        real_device = candidate.Get();
        break;
      }
    }
    if (!real_device ||
        std::ranges::any_of(persona_results, [device_type](const auto& device) {
          return device->DeviceType() == device_type;
        })) {
      continue;
    }

    mappings.Add(persona_device.device_id, real_device->deviceId(),
                 persona_device.group_id, real_device->groupId(), device_type);
    const String label = expose_labels ? persona_device.label : String();
    if (device_type == blink::mojom::MediaDeviceType::kMediaAudioInput ||
        device_type == blink::mojom::MediaDeviceType::kMediaVideoInput) {
      persona_results.push_back(MakeGarbageCollected<InputDeviceInfo>(
          persona_device.device_id, label, persona_device.group_id,
          device_type));
    } else {
      persona_results.push_back(MakeGarbageCollected<MediaDeviceInfo>(
          persona_device.device_id, label, persona_device.group_id,
          device_type));
    }
  }
  *media_devices = std::move(persona_results);
  return true;
}

void TranslateStringConstraint(PersonaMediaDeviceMap& mappings,
                               bool device_id,
                               blink::StringConstraint* constraint) {
  if (constraint->HasExact()) {
    blink::Vector<String> values = constraint->Exact();
    for (auto& value : values) {
      value = device_id ? mappings.RealDeviceId(value)
                        : mappings.RealGroupId(value);
    }
    constraint->SetExact(values);
  }
  if (constraint->HasIdeal()) {
    blink::Vector<String> values = constraint->Ideal();
    for (auto& value : values) {
      value = device_id ? mappings.RealDeviceId(value)
                        : mappings.RealGroupId(value);
    }
    constraint->SetIdeal(values);
  }
}

void TranslateConstraintSet(PersonaMediaDeviceMap& mappings,
                            blink::MediaTrackConstraintSetPlatform* set) {
  TranslateStringConstraint(mappings, true, &set->device_id);
  TranslateStringConstraint(mappings, false, &set->group_id);
}

void TranslateConstraints(PersonaMediaDeviceMap& mappings,
                          blink::MediaConstraints* constraints) {
  if (!constraints || constraints->IsNull()) {
    return;
  }

  blink::MediaTrackConstraintSetPlatform basic = constraints->Basic();
  blink::Vector<blink::MediaTrackConstraintSetPlatform> advanced =
      constraints->Advanced();
  TranslateConstraintSet(mappings, &basic);
  for (auto& set : advanced) {
    TranslateConstraintSet(mappings, &set);
  }

  blink::MediaConstraints translated;
  translated.Initialize(basic, advanced);
  *constraints = translated;
}

void TranslatePersonaMediaDeviceConstraints(ExecutionContext* context,
                                            blink::MediaConstraints* audio,
                                            blink::MediaConstraints* video) {
  if (!context ||
      GetBraveFarblingLevelFor(
          context, ContentSettingsType::BRAVE_WEBCOMPAT_MEDIA_DEVICES,
          BraveFarblingLevel::OFF) == BraveFarblingLevel::OFF ||
      !BraveSessionCache::From(*context).HasPersonaL1()) {
    return;
  }

  PersonaMediaDeviceMap& mappings = PersonaMediaDeviceMap::From(*context);
  TranslateConstraints(mappings, audio);
  TranslateConstraints(mappings, video);
}

void FarbleMediaDevices(ExecutionContext* context,
                        MediaDeviceInfoVector* media_devices) {
  // |media_devices| is guaranteed not to be null here.
  if (GetBraveFarblingLevelFor(
          context, ContentSettingsType::BRAVE_WEBCOMPAT_MEDIA_DEVICES,
          BraveFarblingLevel::OFF) == BraveFarblingLevel::OFF) {
    PersonaMediaDeviceMap::From(*context).Reset();
    return;
  }
  if (UsePersonaMediaDevices(context, media_devices)) {
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
