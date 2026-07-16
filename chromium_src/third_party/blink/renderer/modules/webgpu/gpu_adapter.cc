/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include <optional>

#include "brave/third_party/blink/renderer/core/farbling/brave_session_cache.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"

namespace blink {

namespace {

enum class WebGPUAdapterInfoField {
  kVendor,
  kArchitecture,
  kDevice,
  kDescription,
};

// This class allows to scrub the various device identifiers in the
// GPUAdapterInfo depending on the farbling level.
class BraveScrubWebGpuAdapterInfo {
 public:
  BraveScrubWebGpuAdapterInfo(ExecutionContext* context,
                              String& vendor,
                              String& architecture,
                              String& device,
                              String& description)
      : farbling_level_(brave::GetBraveFarblingLevelFor(
            context,
            ContentSettingsType::BRAVE_WEBCOMPAT_WEBGPU,
            BraveFarblingLevel::OFF)),
        reset_vendor_(
            &vendor,
            ApplyFarbling(context, WebGPUAdapterInfoField::kVendor, vendor)),
        reset_architecture_(&architecture,
                            ApplyFarbling(context,
                                          WebGPUAdapterInfoField::kArchitecture,
                                          architecture)),
        reset_device_(
            &device,
            ApplyFarbling(context, WebGPUAdapterInfoField::kDevice, device)),
        reset_description_(&description,
                           ApplyFarbling(context,
                                         WebGPUAdapterInfoField::kDescription,
                                         description)) {}

  ~BraveScrubWebGpuAdapterInfo() = default;

 private:
  String ApplyFarbling(ExecutionContext* context,
                       WebGPUAdapterInfoField field,
                       const String& s) {
    if (farbling_level_ != BraveFarblingLevel::OFF && context) {
      brave::BraveSessionCache& cache =
          brave::BraveSessionCache::From(*context);
      std::optional<String> persona_value;
      switch (field) {
        case WebGPUAdapterInfoField::kVendor:
          persona_value = cache.PersonaWebGPUVendor();
          break;
        case WebGPUAdapterInfoField::kArchitecture:
          persona_value = cache.PersonaWebGPUArchitecture();
          break;
        case WebGPUAdapterInfoField::kDevice:
          persona_value = cache.PersonaWebGPUDevice();
          break;
        case WebGPUAdapterInfoField::kDescription:
          persona_value = cache.PersonaWebGPUDescription();
          break;
      }
      if (persona_value) {
        return *persona_value;
      }
    }

    switch (farbling_level_) {
      case BraveFarblingLevel::OFF:
        return s;
      case BraveFarblingLevel::BALANCED:
        return base::FeatureList::IsEnabled(
                   blink::features::kWebGLBalancedFingerprintingProtection)
                   ? String()
                   : s;
      case BraveFarblingLevel::MAXIMUM:
        return String();
    }
    return s;
  }

  const BraveFarblingLevel farbling_level_;
  const base::AutoReset<String> reset_vendor_;
  const base::AutoReset<String> reset_architecture_;
  const base::AutoReset<String> reset_device_;
  const base::AutoReset<String> reset_description_;
};

String PersonaWebGPUAdapterInfoValue(ExecutionContext* context,
                                     WebGPUAdapterInfoField field) {
  if (!context || brave::GetBraveFarblingLevelFor(
                      context, ContentSettingsType::BRAVE_WEBCOMPAT_WEBGPU,
                      BraveFarblingLevel::OFF) == BraveFarblingLevel::OFF) {
    return String();
  }

  brave::BraveSessionCache& cache = brave::BraveSessionCache::From(*context);
  std::optional<String> persona_value;
  switch (field) {
    case WebGPUAdapterInfoField::kVendor:
      persona_value = cache.PersonaWebGPUVendor();
      break;
    case WebGPUAdapterInfoField::kArchitecture:
      persona_value = cache.PersonaWebGPUArchitecture();
      break;
    case WebGPUAdapterInfoField::kDevice:
      persona_value = cache.PersonaWebGPUDevice();
      break;
    case WebGPUAdapterInfoField::kDescription:
      persona_value = cache.PersonaWebGPUDescription();
      break;
  }
  return persona_value.value_or(String());
}
}  // namespace

}  // namespace blink

#define BRAVE_SCRUB_WEBGPU_ADAPTER_INFO                                    \
  BraveScrubWebGpuAdapterInfo scrub_guard(gpu_->GetExecutionContext(),     \
                                          vendor_, architecture_, device_, \
                                          description_);

#define BRAVE_WEBGPU_ADAPTER_INFO_EXTRA_ARGS                          \
  ,                                                                   \
      PersonaWebGPUAdapterInfoValue(gpu_->GetExecutionContext(),      \
                                    WebGPUAdapterInfoField::kDevice), \
      PersonaWebGPUAdapterInfoValue(gpu_->GetExecutionContext(),      \
                                    WebGPUAdapterInfoField::kDescription)

#include <third_party/blink/renderer/modules/webgpu/gpu_adapter.cc>

#undef BRAVE_WEBGPU_ADAPTER_INFO_EXTRA_ARGS
#undef BRAVE_SCRUB_WEBGPU_ADAPTER_INFO
