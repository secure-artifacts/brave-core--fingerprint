/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/third_party/blink/renderer/core/farbling/brave_session_cache.h"

#include <algorithm>
#include <array>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>

#include "base/check.h"
#include "base/compiler_specific.h"
#include "base/debug/alias.h"
#include "base/debug/dump_without_crashing.h"
#include "base/feature_list.h"
#include "base/hash/hash.h"
#include "base/notreached.h"
#include "base/numerics/byte_conversions.h"
#include "base/numerics/safe_conversions.h"
#include "brave/third_party/blink/renderer/brave_farbling_constants.h"
#include "brave/third_party/blink/renderer/brave_font_whitelist.h"
#include "build/build_config.h"
#include "crypto/hmac.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/platform/web_content_settings_client.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/events/event_target.h"
#include "third_party/blink/renderer/core/dom/node.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/core/frame/local_dom_window.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/workers/worker_or_worklet_global_scope.h"
#include "third_party/blink/renderer/core/workers/worklet_global_scope.h"
#include "third_party/blink/renderer/platform/fonts/font_fallback_list.h"
#include "third_party/blink/renderer/platform/language.h"
#include "third_party/blink/renderer/platform/network/network_utils.h"
#include "third_party/blink/renderer/platform/storage/blink_storage_key.h"
#include "third_party/blink/renderer/platform/supplementable.h"
#include "third_party/blink/renderer/platform/weborigin/scheme_registry.h"
#include "third_party/blink/renderer/platform/weborigin/security_origin.h"
#include "third_party/blink/renderer/platform/wtf/casting.h"
#include "third_party/blink/renderer/platform/wtf/text/string_builder.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"
#include "third_party/skia/include/core/SkColorType.h"
#include "third_party/skia/include/core/SkImageInfo.h"
#include "url/url_constants.h"

namespace {

constexpr uint64_t zero = 0;
constexpr double maxUInt64AsDouble = static_cast<double>(UINT64_MAX);

constexpr int kFarbledUserAgentMaxExtraSpaces = 5;

// acceptable letters for generating random strings
constexpr std::string_view kLettersForRandomStrings =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

inline uint64_t lfsr_next(uint64_t v) {
  return ((v >> 1) | (((v << 62) ^ (v << 61)) & (~(~zero << 63) << 62)));
}

uint64_t MixPersonaValue(uint64_t value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

void WriteStableRgbMarker(base::span<uint8_t> bytes, uint64_t value) {
  std::array<std::array<uint8_t, 3>, 4> markers;
  for (size_t marker = 0; marker < markers.size(); ++marker) {
    const uint8_t marker_value = static_cast<uint8_t>(value + marker * 53u);
    markers[marker].fill(marker_value);
  }

  for (const auto& marker : markers) {
    if (std::equal(marker.begin(), marker.end(), bytes.begin())) {
      return;
    }
  }

  const size_t start = static_cast<size_t>((value >> 32u) & 0x3u);
  for (size_t offset = 0; offset < markers.size(); ++offset) {
    const auto& marker = markers[(start + offset) % markers.size()];
    if (marker[0] != bytes[0] && marker[1] != bytes[1] &&
        marker[2] != bytes[2]) {
      std::copy(marker.begin(), marker.end(), bytes.begin());
      return;
    }
  }
  NOTREACHED();
}

void WriteLowBits16(base::span<uint8_t> bytes,
                    uint64_t value,
                    bool skip_non_finite,
                    bool normalized) {
  for (size_t channel = 0; channel < 3u; ++channel) {
    auto channel_bytes = bytes.subspan(channel * 2u).first<2u>();
    uint16_t bits = base::U16FromLittleEndian(channel_bytes);
    if (skip_non_finite && (bits & 0x7c00u) == 0x7c00u) {
      continue;
    }
    const uint16_t low_bits =
        static_cast<uint16_t>((value >> (channel * 3u)) & 0x7u);
    if (normalized && bits == 0x3c00u) {
      bits = static_cast<uint16_t>(0x3bf8u | low_bits);
    } else {
      bits = static_cast<uint16_t>((bits & 0xfff8u) | low_bits);
    }
    channel_bytes.copy_from(base::U16ToLittleEndian(bits));
  }
}

void WriteLowBits32(base::span<uint8_t> bytes,
                    uint64_t value,
                    bool floating_point) {
  for (size_t channel = 0; channel < 3u; ++channel) {
    auto channel_bytes = bytes.subspan(channel * 4u).first<4u>();
    uint32_t bits = base::U32FromLittleEndian(channel_bytes);
    if (floating_point && (bits & 0x7f800000u) == 0x7f800000u) {
      continue;
    }
    bits = (bits & 0xfffffff8u) |
           static_cast<uint32_t>((value >> (channel * 3u)) & 0x7u);
    channel_bytes.copy_from(base::U32ToLittleEndian(bits));
  }
}

void WritePacked101010LowBits(base::span<uint8_t> bytes, uint64_t value) {
  uint32_t bits = base::U32FromLittleEndian(bytes.first<4u>());
  for (size_t channel = 0; channel < 3u; ++channel) {
    const size_t shift = channel * 10u;
    bits = (bits & ~(0x7u << shift)) |
           (static_cast<uint32_t>((value >> (channel * 3u)) & 0x7u) << shift);
  }
  bytes.first<4u>().copy_from(base::U32ToLittleEndian(bits));
}

void WriteLowBits10x6(base::span<uint8_t> bytes, uint64_t value) {
  uint64_t bits = base::U64FromLittleEndian(bytes.first<8u>());
  for (size_t channel = 0; channel < 3u; ++channel) {
    const size_t shift = 6u + channel * 16u;
    bits = (bits & ~(uint64_t{0x7u} << shift)) |
           ((value >> (channel * 3u) & 0x7u) << shift);
  }
  bytes.first<8u>().copy_from(base::U64ToLittleEndian(bits));
}

bool PerturbPersonaPixel(base::span<uint8_t> pixel,
                         SkColorType color_type,
                         uint64_t value) {
  switch (color_type) {
    case kRGBA_8888_SkColorType:
    case kRGB_888x_SkColorType:
    case kBGRA_8888_SkColorType:
    case kSRGBA_8888_SkColorType:
      if (pixel.size() < 4u) {
        return false;
      }
      WriteStableRgbMarker(pixel.first<3u>(), value);
      return true;
    case kRGBA_1010102_SkColorType:
    case kBGRA_1010102_SkColorType:
    case kRGB_101010x_SkColorType:
    case kBGR_101010x_SkColorType:
      if (pixel.size() < 4u) {
        return false;
      }
      WritePacked101010LowBits(pixel.first<4u>(), value);
      return true;
    case kRGBA_F16Norm_SkColorType:
    case kRGBA_F16_SkColorType:
    case kRGB_F16F16F16x_SkColorType:
      if (pixel.size() < 8u) {
        return false;
      }
      WriteLowBits16(pixel.first<8u>(), value, true,
                     color_type == kRGBA_F16Norm_SkColorType);
      return true;
    case kRGBA_10x6_SkColorType:
      if (pixel.size() < 8u) {
        return false;
      }
      WriteLowBits10x6(pixel.first<8u>(), value);
      return true;
    case kR16G16B16A16_unorm_SkColorType:
      if (pixel.size() < 8u) {
        return false;
      }
      WriteLowBits16(pixel.first<8u>(), value, false, false);
      return true;
    case kRGBA_F32_SkColorType:
      if (pixel.size() < 16u) {
        return false;
      }
      WriteLowBits32(pixel.first<16u>(), value, true);
      return true;
    default:
      return false;
  }
}

std::optional<blink::String> BlinkStringFromUtf8IfNotEmpty(
    const std::string& value) {
  if (value.empty()) {
    return std::nullopt;
  }
  return blink::String::FromUtf8(base::as_byte_span(value));
}

blink::WebContentSettingsClient* GetContentSettings(
    blink::LocalFrame* local_frame) {
  return local_frame ? local_frame->LocalFrameRoot().GetContentSettingsClient()
                     : nullptr;
}

// 1pes mode and anonymous frames add a StorageKey nonce, which affects the
// non-persona farbling token.
const blink::BlinkStorageKey* GetStorageKey(blink::ExecutionContext* context) {
  if (!context) {
    return nullptr;
  }

  if (auto* window = blink::DynamicTo<blink::LocalDOMWindow>(context)) {
    return &window->GetStorageKey();
  }

  if (auto* worklet = blink::DynamicTo<blink::WorkletGlobalScope>(context)) {
    if (worklet->IsMainThreadWorkletGlobalScope()) {
      if (auto* frame = worklet->GetFrame()) {
        if (auto* document = frame->DomWindow()) {
          return &document->GetStorageKey();
        }
      }
    }
  }

  return nullptr;
}

}  // namespace

namespace brave {

using brave_shields::FarblingPRNG;

constexpr char BraveSessionCache::kSupplementName[] = "BraveSessionCache";

blink::WebContentSettingsClient* GetContentSettingsClientFor(
    ExecutionContext* context) {
  if (!context) {
    return nullptr;
  }

  // Avoid blocking fingerprinting in WebUI and local documents.
  const blink::String protocol = context->GetSecurityOrigin()
                                     ->GetOriginOrPrecursorOriginIfOpaque()
                                     ->Protocol();
  static constexpr const char* kExcludedProtocols[] = {
      url::kFileScheme,
      "chrome-untrusted",
  };
  if (protocol.empty() || std::ranges::contains(kExcludedProtocols, protocol) ||
      (protocol != "chrome-extension" &&
       blink::SchemeRegistry::ShouldTreatURLSchemeAsDisplayIsolated(
           protocol))) {
    return nullptr;
  }

  if (auto* window = blink::DynamicTo<blink::LocalDOMWindow>(context)) {
    if (auto* content_settings =
            GetContentSettings(window->GetDisconnectedFrame())) {
      return content_settings;
    }

    if (auto* content_settings = GetContentSettings(window->GetFrame())) {
      return content_settings;
    }

    // This may happen in some cases, e.g. when IsolatedSVGDocument is used.
    return nullptr;
  }

  if (auto* worker_or_worklet =
          blink::DynamicTo<blink::WorkerOrWorkletGlobalScope>(context)) {
    return worker_or_worklet->ContentSettingsClient();
  }

  DEBUG_ALIAS_FOR_OBJECT(context_alias, context);
  NOTREACHED() << "Unhandled ExecutionContext type";
}

BraveFarblingLevel GetBraveFarblingLevelFor(
    ExecutionContext* context,
    ContentSettingsType webcompat_settings_type,
    BraveFarblingLevel default_value) {
  BraveFarblingLevel value = default_value;
  if (context) {
    value = brave::BraveSessionCache::From(*context).GetBraveFarblingLevel(
        webcompat_settings_type);
  }
  return value;
}

bool AllowFingerprinting(ExecutionContext* context,
                         ContentSettingsType webcompat_settings_type) {
  return (GetBraveFarblingLevelFor(context, webcompat_settings_type,
                                   BraveFarblingLevel::OFF) !=
          BraveFarblingLevel::MAXIMUM);
}

bool AllowFontFamily(ExecutionContext* context,
                     const blink::AtomicString& family_name) {
  if (!context) {
    return true;
  }

  auto* settings = brave::GetContentSettingsClientFor(context);
  if (!settings) {
    return true;
  }

  if (!brave::BraveSessionCache::From(*context).AllowFontFamily(settings,
                                                                family_name)) {
    return false;
  }

  return true;
}

int FarbleInteger(ExecutionContext* context,
                  brave::FarbleKey key,
                  int spoof_value,
                  int min_value,
                  int max_value) {
  BraveSessionCache& cache = BraveSessionCache::From(*context);
  return cache.FarbledInteger(key, spoof_value, min_value, max_value);
}

bool BlockScreenFingerprinting(ExecutionContext* context,
                               bool early /* = false */) {
  if (!base::FeatureList::IsEnabled(
          blink::features::kBraveBlockScreenFingerprinting)) {
    return false;
  }
  BraveFarblingLevel level = GetBraveFarblingLevelFor(
      context,
      early ? ContentSettingsType::BRAVE_WEBCOMPAT_NONE
            : ContentSettingsType::BRAVE_WEBCOMPAT_SCREEN,
      BraveFarblingLevel::OFF);
  return level != BraveFarblingLevel::OFF;
}

const DOMWindow* EventTargetToDOMWindow(blink::EventTarget* target) {
  if (!target) {
    return nullptr;
  }
  if (const DOMWindow* window = target->ToDOMWindow()) {
    return window;
  }
  if (blink::Node* node = target->ToNode()) {
    return node->GetDocument().domWindow();
  }
  return nullptr;
}

double FarbledPointerScreenCoordinate(const DOMWindow* view,
                                      FarbleKey key,
                                      double client_coordinate,
                                      double true_screen_coordinate) {
  const blink::LocalDOMWindow* local_dom_window =
      blink::DynamicTo<blink::LocalDOMWindow>(view);
  if (!local_dom_window) {
    return true_screen_coordinate;
  }
  ExecutionContext* context = local_dom_window->GetExecutionContext();
  if (!BlockScreenFingerprinting(context)) {
    return true_screen_coordinate;
  }
  BraveSessionCache& cache = BraveSessionCache::From(*context);
  switch (key) {
    case FarbleKey::kPointerScreenX:
      if (auto value = cache.PersonaWindowX()) {
        return *value + client_coordinate;
      }
      break;
    case FarbleKey::kPointerScreenY:
      if (auto value = cache.PersonaWindowY()) {
        return *value + client_coordinate;
      }
      break;
    default:
      break;
  }
  auto* frame = local_dom_window->GetFrame();
  if (!frame) {
    return true_screen_coordinate;
  }
  double zoom_factor = frame->LayoutZoomFactor();
  return FarbleInteger(context, key,
                       static_cast<int>(zoom_factor * client_coordinate), 0, 8);
}

BraveSessionCache::BraveSessionCache(ExecutionContext& context)
    : Supplement<ExecutionContext>(context) {
  if (auto* settings_client = GetContentSettingsClientFor(&context)) {
    default_shields_settings_ = settings_client->GetBraveShieldsSettings(
        ContentSettingsType::BRAVE_WEBCOMPAT_NONE);
    if (!default_shields_settings_) {
      DEBUG_ALIAS_FOR_OBJECT(settings_client_alias, settings_client);
      base::debug::DumpWithoutCrashing();
      default_shields_settings_ = brave_shields::mojom::ShieldsSettings::New();
    }
  } else {
    default_shields_settings_ = brave_shields::mojom::ShieldsSettings::New();
  }

  if (const auto* storage_key = GetStorageKey(&context);
      storage_key && storage_key->GetNonce() &&
      !storage_key->GetNonce()->is_empty() &&
      !default_shields_settings_->has_persona_farbling_token) {
    // Use storage key nonce hash to XOR the existing farbling token. Do not use
    // the nonce directly to not accidentaly leak it somehow via farbled values.
    const size_t storage_key_nonce_hash =
        base::FastHash(storage_key->GetNonce()->AsBytes());
    default_shields_settings_->farbling_token =
        base::Token(default_shields_settings_->farbling_token.high() ^
                        storage_key_nonce_hash,
                    default_shields_settings_->farbling_token.low() ^
                        storage_key_nonce_hash);
  }
}

BraveSessionCache& BraveSessionCache::From(ExecutionContext& context) {
  BraveSessionCache* cache =
      Supplement<ExecutionContext>::From<BraveSessionCache>(context);
  if (!cache) {
    cache = MakeGarbageCollected<BraveSessionCache>(context);
    ProvideTo(context, cache);
  }
  return *cache;
}

// static
void BraveSessionCache::Init() {
  RegisterAllowFontFamilyCallback(base::BindRepeating(&brave::AllowFontFamily));
}

blink::WebGLFarbledExtensionHandler*
BraveSessionCache::CreateWebGLFarbledExtensionHandler(
    const blink::Vector<blink::String>& supported_extensions) {
  CHECK(!webgl_farbled_extension_handler_);

  auto level =
      GetBraveFarblingLevel(ContentSettingsType::BRAVE_WEBCOMPAT_WEBGL);
  webgl_farbled_extension_handler_ =
      level == BraveFarblingLevel::OFF
          ? blink::WebGLFarbledExtensionHandler::CreateOffHandler(
                supported_extensions)
      : level == BraveFarblingLevel::BALANCED
          ? blink::WebGLFarbledExtensionHandler::CreateBalancedHandler(
                supported_extensions,
                default_shields_settings_->farbling_token.low())
          : blink::WebGLFarbledExtensionHandler::CreateMaximumHandler(
                supported_extensions);

  return webgl_farbled_extension_handler_.get();
}

std::optional<blink::BraveAudioFarblingHelper>
BraveSessionCache::GetAudioFarblingHelper() {
  const auto audio_farbling_level =
      GetBraveFarblingLevel(ContentSettingsType::BRAVE_WEBCOMPAT_AUDIO);
  if (audio_farbling_level == BraveFarblingLevel::OFF) {
    return std::nullopt;
  }
  if (!audio_farbling_helper_) {
    // This call is only expensive the first time; afterwards it returns
    // a cached value:
    const uint64_t fudge = default_shields_settings_->farbling_token.high();
    const double fudge_factor = 0.999 + ((fudge / maxUInt64AsDouble) / 1000);
    const uint64_t seed = default_shields_settings_->farbling_token.low();
    audio_farbling_helper_.emplace(
        fudge_factor, seed,
        audio_farbling_level == BraveFarblingLevel::MAXIMUM);
  }
  return audio_farbling_helper_;
}

void BraveSessionCache::FarbleAudioChannel(base::span<float> dst,
                                           size_t sample_offset) {
  const auto& audio_farbling_helper = GetAudioFarblingHelper();
  if (audio_farbling_helper) {
    if (default_shields_settings_->has_persona_farbling_token) {
      audio_farbling_helper->FarbleAudioChannelForPersona(dst, sample_offset);
    } else {
      audio_farbling_helper->FarbleAudioChannel(dst);
    }
  }
}

void BraveSessionCache::FarbleAudioChannelCopy(base::span<float> source,
                                               base::span<float> destination,
                                               size_t sample_offset) {
  CHECK_EQ(source.size(), destination.size());
  const auto& audio_farbling_helper = GetAudioFarblingHelper();
  if (!audio_farbling_helper) {
    return;
  }
  if (!default_shields_settings_->has_persona_farbling_token) {
    audio_farbling_helper->FarbleAudioChannel(destination);
    return;
  }
  audio_farbling_helper->FarbleAudioChannelForPersona(source, sample_offset);
  destination.copy_from(source);
}

void BraveSessionCache::PerturbCanvasPixels(base::span<uint8_t> data,
                                            int surface_width,
                                            int surface_height,
                                            int read_x,
                                            int read_y,
                                            int read_width,
                                            int read_height,
                                            size_t row_bytes,
                                            int color_type) {
  if (GetBraveFarblingLevel(ContentSettingsType::BRAVE_WEBCOMPAT_CANVAS) ==
      BraveFarblingLevel::OFF) {
    return;
  }
  if (!default_shields_settings_->has_persona_farbling_token) {
    PerturbPixelsInternal(data);
    return;
  }
  PerturbCanvasPixelsForPersona(data, surface_width, surface_height, read_x,
                                read_y, read_width, read_height, row_bytes,
                                color_type);
}

void BraveSessionCache::PerturbCanvasPixelsForPersona(base::span<uint8_t> data,
                                                      int surface_width,
                                                      int surface_height,
                                                      int read_x,
                                                      int read_y,
                                                      int read_width,
                                                      int read_height,
                                                      size_t row_bytes,
                                                      int color_type_value) {
  if (data.empty() || surface_width <= 0 || surface_height <= 0 ||
      read_width <= 0 || read_height <= 0) {
    return;
  }

  const size_t surface_pixel_count =
      base::checked_cast<size_t>(surface_width) * surface_height;
  if (color_type_value < 0 || color_type_value >= kSkColorTypeCnt) {
    return;
  }
  const auto color_type = static_cast<SkColorType>(color_type_value);
  const size_t bytes_per_pixel = SkColorTypeBytesPerPixel(color_type);
  const size_t row_pixel_bytes =
      base::checked_cast<size_t>(read_width) * bytes_per_pixel;
  const size_t last_row_offset =
      base::checked_cast<size_t>(read_height - 1) * row_bytes;
  if (bytes_per_pixel == 0u || row_bytes < row_pixel_bytes ||
      last_row_offset > data.size() ||
      row_pixel_bytes > data.size() - last_row_offset) {
    return;
  }

  const size_t watermark_count =
      std::clamp(1u + (surface_pixel_count - 1u) / 64u, size_t{1}, size_t{32});
  const uint64_t token_seed = default_shields_settings_->farbling_token.low() ^
                              default_shields_settings_->farbling_token.high();
  const size_t start = MixPersonaValue(token_seed) % surface_pixel_count;
  size_t step = 0;
  if (surface_pixel_count > 1u) {
    step = 1u + (MixPersonaValue(token_seed ^ 0x726561646261636bULL) %
                 (surface_pixel_count - 1u));
    while (std::gcd(step, surface_pixel_count) != 1u) {
      step = step + 1u == surface_pixel_count ? 1u : step + 1u;
    }
  }
  const uint64_t watermark_salt =
      MixPersonaValue(token_seed ^ 0x77617465726d6172ULL);
  const int64_t read_left = read_x;
  const int64_t read_top = read_y;
  const int64_t read_right = read_left + read_width;
  const int64_t read_bottom = read_top + read_height;

  size_t surface_index = start;
  for (size_t i = 0; i < watermark_count; ++i) {
    const int x = static_cast<int>(surface_index % surface_width);
    const int y = static_cast<int>(surface_index / surface_width);
    if (x >= read_left && x < read_right && y >= read_top && y < read_bottom) {
      const size_t local_x =
          base::checked_cast<size_t>(static_cast<int64_t>(x) - read_left);
      const size_t local_y =
          base::checked_cast<size_t>(static_cast<int64_t>(y) - read_top);
      const size_t pixel_offset =
          local_y * row_bytes + local_x * bytes_per_pixel;
      const uint64_t pixel_value =
          MixPersonaValue(watermark_salt ^ surface_index);
      PerturbPersonaPixel(data.subspan(pixel_offset, bytes_per_pixel),
                          color_type, pixel_value);
    }
    surface_index = (surface_index + step) % surface_pixel_count;
  }
}

void BraveSessionCache::PerturbPixels(
    base::span<uint8_t> data,
    ContentSettingsType webcompat_settings_type) {
  if (GetBraveFarblingLevel(webcompat_settings_type) ==
      BraveFarblingLevel::OFF) {
    return;
  }
  PerturbPixelsInternal(data);
}

void BraveSessionCache::PerturbPixelsInternal(base::span<uint8_t> data) {
  if (data.empty()) {
    return;
  }

  // This needs to be type size_t because we pass it to std::string_view
  // later for content hashing. This is safe because the maximum canvas
  // dimensions are less than SIZE_T_MAX. (Width and height are each
  // limited to 32,767 pixels.)
  // Four bits per pixel
  const size_t pixel_count = data.size() / 4;

  // calculate initial seed to find first pixel to perturb, based on session
  // key, domain key, and canvas contents
  auto canvas_key = crypto::hmac::SignSha256(
      default_shields_settings_->farbling_token.AsBytes(), data);
  uint64_t v = base::U64FromNativeEndian(base::span(canvas_key).first<8u>());
  // iterate through 32-byte canvas key and use each bit to determine how to
  // perturb the current pixel
  for (uint8_t key : canvas_key) {
    uint8_t bit = key;
    for (int j = 0; j < 16; j++) {
      if (j % 8 == 0) {
        bit = key;
      }
      // choose which channel (R, G, or B) to perturb
      uint8_t channel = v % 3;
      uint64_t pixel_index = 4 * (v % pixel_count) + channel;
      data[pixel_index] = data[pixel_index] ^ (bit & 0x1);
      bit = bit >> 1;
      // find next pixel to perturb
      v = lfsr_next(v);
    }
  }
}

blink::String BraveSessionCache::GenerateRandomString(
    std::string_view seed,
    blink::wtf_size_t length) {
  auto key = crypto::hmac::SignSha256(
      default_shields_settings_->farbling_token.AsBytes(),
      base::as_byte_span(seed));
  // initial PRNG seed based on session key and passed-in seed string
  uint64_t v = base::U64FromNativeEndian(base::span(key).first<8u>());
  base::span<UChar> destination;
  blink::String value = blink::String::CreateUninitialized(length, destination);
  for (auto& c : destination) {
    c = kLettersForRandomStrings.at(v % kLettersForRandomStrings.size());
    v = lfsr_next(v);
  }
  return value;
}

blink::String BraveSessionCache::FarbledUserAgent(
    blink::String real_user_agent) {
  FarblingPRNG prng = MakePseudoRandomGenerator();
  blink::StringBuilder result;
  result.Append(real_user_agent);
  int extra = prng() % kFarbledUserAgentMaxExtraSpaces;
  for (int i = 0; i < extra; i++) {
    result.Append(" ");
  }
  return result.ToString();
}

bool BraveSessionCache::HasPersonaL1() const {
  return default_shields_settings_ && default_shields_settings_->has_persona_l1;
}

std::optional<blink::String> BraveSessionCache::PersonaUserAgent() const {
  if (!HasPersonaL1() ||
      default_shields_settings_->persona_user_agent.empty()) {
    return std::nullopt;
  }
  return blink::String::FromUtf8(
      base::as_byte_span(default_shields_settings_->persona_user_agent));
}

std::optional<blink::String> BraveSessionCache::PersonaPlatform() const {
  if (!HasPersonaL1() || default_shields_settings_->persona_platform.empty()) {
    return std::nullopt;
  }
  return blink::String::FromUtf8(
      base::as_byte_span(default_shields_settings_->persona_platform));
}

std::optional<blink::Vector<blink::String>>
BraveSessionCache::PersonaLanguages() const {
  if (!HasPersonaL1() || default_shields_settings_->persona_languages.empty()) {
    return std::nullopt;
  }
  blink::Vector<blink::String> languages;
  languages.ReserveInitialCapacity(
      default_shields_settings_->persona_languages.size());
  for (const auto& language : default_shields_settings_->persona_languages) {
    if (!language.empty()) {
      languages.push_back(
          blink::String::FromUtf8(base::as_byte_span(language)));
    }
  }
  if (languages.empty()) {
    return std::nullopt;
  }
  return languages;
}

std::optional<blink::UserAgentMetadata>
BraveSessionCache::PersonaUserAgentMetadata() const {
  if (!HasPersonaL1() ||
      default_shields_settings_->persona_ua_full_version.empty() ||
      default_shields_settings_->persona_ua_platform_version.empty() ||
      default_shields_settings_->persona_ua_brand_names.empty() ||
      default_shields_settings_->persona_ua_brand_names.size() !=
          default_shields_settings_->persona_ua_brand_versions.size() ||
      default_shields_settings_->persona_ua_full_version_brand_names.empty() ||
      default_shields_settings_->persona_ua_full_version_brand_names.size() !=
          default_shields_settings_->persona_ua_full_version_brand_versions
              .size()) {
    return std::nullopt;
  }

  const char* ua_platform = nullptr;
  if (default_shields_settings_->persona_platform == "MacIntel") {
    ua_platform = "macOS";
  } else if (default_shields_settings_->persona_platform.starts_with("Win")) {
    ua_platform = "Windows";
  } else {
    return std::nullopt;
  }

  blink::UserAgentMetadata metadata;
  for (size_t i = 0;
       i < default_shields_settings_->persona_ua_brand_names.size(); ++i) {
    metadata.brand_version_list.emplace_back(
        default_shields_settings_->persona_ua_brand_names[i],
        default_shields_settings_->persona_ua_brand_versions[i]);
  }
  for (size_t i = 0;
       i <
       default_shields_settings_->persona_ua_full_version_brand_names.size();
       ++i) {
    metadata.brand_full_version_list.emplace_back(
        default_shields_settings_->persona_ua_full_version_brand_names[i],
        default_shields_settings_->persona_ua_full_version_brand_versions[i]);
  }
  metadata.full_version = default_shields_settings_->persona_ua_full_version;
  metadata.platform = ua_platform;
  metadata.platform_version =
      default_shields_settings_->persona_ua_platform_version;
  metadata.architecture = default_shields_settings_->persona_ua_architecture;
  metadata.bitness = default_shields_settings_->persona_ua_bitness;
  metadata.mobile = default_shields_settings_->persona_ua_mobile;
  metadata.form_factors = {metadata.mobile ? blink::kMobileFormFactor
                                           : blink::kDesktopFormFactor};
  return metadata;
}

std::optional<unsigned> BraveSessionCache::PersonaHardwareConcurrency() const {
  if (!HasPersonaL1() ||
      default_shields_settings_->persona_hardware_concurrency == 0) {
    return std::nullopt;
  }
  return default_shields_settings_->persona_hardware_concurrency;
}

std::optional<double> BraveSessionCache::PersonaDeviceMemory() const {
  if (!HasPersonaL1() ||
      default_shields_settings_->persona_device_memory_gb <= 0.0) {
    return std::nullopt;
  }
  return default_shields_settings_->persona_device_memory_gb;
}

std::optional<unsigned> BraveSessionCache::PersonaMaxTouchPoints() const {
  if (!HasPersonaL1()) {
    return std::nullopt;
  }
  return default_shields_settings_->persona_max_touch_points;
}

bool BraveSessionCache::HasPersonaScreen() const {
  if (!HasPersonaL1()) {
    return false;
  }
  return default_shields_settings_->persona_screen_width > 0 &&
         default_shields_settings_->persona_screen_height > 0 &&
         default_shields_settings_->persona_screen_avail_width > 0 &&
         default_shields_settings_->persona_screen_avail_height > 0 &&
         default_shields_settings_->persona_screen_avail_width <=
             default_shields_settings_->persona_screen_width &&
         default_shields_settings_->persona_screen_avail_height <=
             default_shields_settings_->persona_screen_height &&
         default_shields_settings_->persona_screen_color_depth > 0 &&
         default_shields_settings_->persona_screen_device_scale_factor > 0.0;
}

std::optional<int> BraveSessionCache::PersonaScreenWidth() const {
  if (!HasPersonaScreen()) {
    return std::nullopt;
  }
  return default_shields_settings_->persona_screen_width;
}

std::optional<int> BraveSessionCache::PersonaScreenHeight() const {
  if (!HasPersonaScreen()) {
    return std::nullopt;
  }
  return default_shields_settings_->persona_screen_height;
}

std::optional<int> BraveSessionCache::PersonaScreenAvailWidth() const {
  if (!HasPersonaScreen()) {
    return std::nullopt;
  }
  return default_shields_settings_->persona_screen_avail_width;
}

std::optional<int> BraveSessionCache::PersonaScreenAvailHeight() const {
  if (!HasPersonaScreen()) {
    return std::nullopt;
  }
  return default_shields_settings_->persona_screen_avail_height;
}

std::optional<int> BraveSessionCache::PersonaScreenColorDepth() const {
  if (!HasPersonaScreen()) {
    return std::nullopt;
  }
  return default_shields_settings_->persona_screen_color_depth;
}

std::optional<double> BraveSessionCache::PersonaScreenDeviceScaleFactor()
    const {
  if (!HasPersonaScreen()) {
    return std::nullopt;
  }
  return default_shields_settings_->persona_screen_device_scale_factor;
}

std::optional<int> BraveSessionCache::PersonaWindowX() const {
  if (!HasPersonaScreen()) {
    return std::nullopt;
  }
  return default_shields_settings_->persona_window_x;
}

std::optional<int> BraveSessionCache::PersonaWindowY() const {
  if (!HasPersonaScreen()) {
    return std::nullopt;
  }
  return default_shields_settings_->persona_window_y;
}

bool BraveSessionCache::HasPersonaL2() const {
  return default_shields_settings_ && default_shields_settings_->has_persona_l2;
}

std::optional<blink::String> BraveSessionCache::PersonaWebGLVendor() const {
  if (!HasPersonaL2()) {
    return std::nullopt;
  }
  return BlinkStringFromUtf8IfNotEmpty(
      default_shields_settings_->persona_webgl_vendor);
}

std::optional<blink::String> BraveSessionCache::PersonaWebGLRenderer() const {
  if (!HasPersonaL2()) {
    return std::nullopt;
  }
  return BlinkStringFromUtf8IfNotEmpty(
      default_shields_settings_->persona_webgl_renderer);
}

std::optional<blink::String> BraveSessionCache::PersonaWebGPUVendor() const {
  if (!HasPersonaL2()) {
    return std::nullopt;
  }
  return BlinkStringFromUtf8IfNotEmpty(
      default_shields_settings_->persona_webgpu_vendor);
}

std::optional<blink::String> BraveSessionCache::PersonaWebGPUArchitecture()
    const {
  if (!HasPersonaL2()) {
    return std::nullopt;
  }
  return BlinkStringFromUtf8IfNotEmpty(
      default_shields_settings_->persona_webgpu_architecture);
}

std::optional<blink::String> BraveSessionCache::PersonaWebGPUDevice() const {
  if (!HasPersonaL2()) {
    return std::nullopt;
  }
  return BlinkStringFromUtf8IfNotEmpty(
      default_shields_settings_->persona_webgpu_device);
}

std::optional<blink::String> BraveSessionCache::PersonaWebGPUDescription()
    const {
  if (!HasPersonaL2()) {
    return std::nullopt;
  }
  return BlinkStringFromUtf8IfNotEmpty(
      default_shields_settings_->persona_webgpu_description);
}

bool BraveSessionCache::HasPersonaFontSet() const {
  return default_shields_settings_ &&
         !default_shields_settings_->persona_fonts.empty();
}

std::optional<blink::Vector<PersonaMediaDeviceValue>>
BraveSessionCache::PersonaMediaDevices() const {
  if (!HasPersonaL1()) {
    return std::nullopt;
  }

  const auto& kinds = default_shields_settings_->persona_media_device_kinds;
  const auto& ids = default_shields_settings_->persona_media_device_ids;
  const auto& labels = default_shields_settings_->persona_media_device_labels;
  const auto& group_ids =
      default_shields_settings_->persona_media_device_group_ids;
  if (kinds.empty() || kinds.size() != ids.size() ||
      kinds.size() != labels.size() || kinds.size() != group_ids.size()) {
    return std::nullopt;
  }

  blink::Vector<PersonaMediaDeviceValue> devices;
  devices.ReserveInitialCapacity(kinds.size());
  for (blink::wtf_size_t i = 0; i < kinds.size(); ++i) {
    auto kind = BlinkStringFromUtf8IfNotEmpty(kinds[i]);
    auto id = BlinkStringFromUtf8IfNotEmpty(ids[i]);
    auto label = BlinkStringFromUtf8IfNotEmpty(labels[i]);
    auto group_id = BlinkStringFromUtf8IfNotEmpty(group_ids[i]);
    if (!kind || !id || !label || !group_id) {
      return std::nullopt;
    }
    devices.push_back(PersonaMediaDeviceValue{*kind, *id, *label, *group_id});
  }
  return devices;
}

std::optional<blink::Vector<PersonaSpeechVoiceValue>>
BraveSessionCache::PersonaSpeechVoices() const {
  if (!HasPersonaL1()) {
    return std::nullopt;
  }

  const auto& uris = default_shields_settings_->persona_speech_voice_uris;
  const auto& names = default_shields_settings_->persona_speech_voice_names;
  const auto& langs = default_shields_settings_->persona_speech_voice_langs;
  const auto& local_services =
      default_shields_settings_->persona_speech_voice_local_services;
  const auto& defaults =
      default_shields_settings_->persona_speech_voice_defaults;
  if (uris.empty() || uris.size() != names.size() ||
      uris.size() != langs.size() || uris.size() != local_services.size() ||
      uris.size() != defaults.size()) {
    return std::nullopt;
  }

  blink::Vector<PersonaSpeechVoiceValue> voices;
  voices.ReserveInitialCapacity(uris.size());
  for (blink::wtf_size_t i = 0; i < uris.size(); ++i) {
    auto uri = BlinkStringFromUtf8IfNotEmpty(uris[i]);
    auto name = BlinkStringFromUtf8IfNotEmpty(names[i]);
    auto lang = BlinkStringFromUtf8IfNotEmpty(langs[i]);
    if (!uri || !name || !lang) {
      return std::nullopt;
    }
    voices.push_back(PersonaSpeechVoiceValue{*uri, *name, *lang,
                                             local_services[i], defaults[i]});
  }
  return voices;
}

std::optional<blink::Vector<PersonaPluginValue>>
BraveSessionCache::PersonaPlugins() const {
  if (!HasPersonaL1()) {
    return std::nullopt;
  }

  const auto& names = default_shields_settings_->persona_plugin_names;
  const auto& filenames = default_shields_settings_->persona_plugin_filenames;
  const auto& descriptions =
      default_shields_settings_->persona_plugin_descriptions;
  const auto& mime_counts =
      default_shields_settings_->persona_plugin_mime_type_counts;
  const auto& mime_types = default_shields_settings_->persona_mime_type_types;
  const auto& mime_descriptions =
      default_shields_settings_->persona_mime_type_descriptions;
  const auto& mime_suffixes =
      default_shields_settings_->persona_mime_type_suffixes;
  if (names.empty() || names.size() != filenames.size() ||
      names.size() != descriptions.size() ||
      names.size() != mime_counts.size() ||
      mime_types.size() != mime_descriptions.size() ||
      mime_types.size() != mime_suffixes.size()) {
    return std::nullopt;
  }

  size_t expected_mime_count = 0;
  for (uint32_t count : mime_counts) {
    if (count == 0) {
      return std::nullopt;
    }
    expected_mime_count += count;
  }
  if (expected_mime_count != mime_types.size()) {
    return std::nullopt;
  }

  blink::Vector<PersonaPluginValue> plugins;
  plugins.ReserveInitialCapacity(names.size());
  size_t mime_index = 0;
  for (blink::wtf_size_t plugin_index = 0; plugin_index < names.size();
       ++plugin_index) {
    auto name = BlinkStringFromUtf8IfNotEmpty(names[plugin_index]);
    auto filename = BlinkStringFromUtf8IfNotEmpty(filenames[plugin_index]);
    auto description =
        BlinkStringFromUtf8IfNotEmpty(descriptions[plugin_index]);
    if (!name || !filename || !description) {
      return std::nullopt;
    }

    PersonaPluginValue plugin{*name, *filename, *description, {}};
    plugin.mime_types.ReserveInitialCapacity(mime_counts[plugin_index]);
    for (uint32_t offset = 0; offset < mime_counts[plugin_index]; ++offset) {
      auto type = BlinkStringFromUtf8IfNotEmpty(mime_types[mime_index]);
      auto mime_description =
          BlinkStringFromUtf8IfNotEmpty(mime_descriptions[mime_index]);
      if (!type || !mime_description || mime_suffixes[mime_index].empty()) {
        return std::nullopt;
      }
      blink::Vector<blink::String> suffixes;
      suffixes.ReserveInitialCapacity(mime_suffixes[mime_index].size());
      for (const auto& suffix : mime_suffixes[mime_index]) {
        auto value = BlinkStringFromUtf8IfNotEmpty(suffix);
        if (!value) {
          return std::nullopt;
        }
        suffixes.push_back(*value);
      }
      plugin.mime_types.push_back(
          PersonaMimeTypeValue{*type, *mime_description, std::move(suffixes)});
      ++mime_index;
    }
    plugins.push_back(std::move(plugin));
  }
  return plugins;
}

int BraveSessionCache::FarbledInteger(FarbleKey key,
                                      int spoof_value,
                                      int min_random_offset,
                                      int max_random_offset) {
  auto item = farbled_integers_.find(key);
  if (item == farbled_integers_.end()) {
    FarblingPRNG prng = MakePseudoRandomGenerator(key);
    auto added = farbled_integers_.insert(
        key, base::checked_cast<int>(
                 prng() % (1 + max_random_offset - min_random_offset) +
                 min_random_offset));

    return added.stored_value->value + spoof_value;
  }
  return item->value + spoof_value;
}

bool BraveSessionCache::AllowFontFamily(
    blink::WebContentSettingsClient* settings,
    const blink::AtomicString& family_name) {
  if (!settings ||
      GetBraveFarblingLevel(ContentSettingsType::BRAVE_WEBCOMPAT_FONT) ==
          BraveFarblingLevel::OFF) {
    return true;
  }
  if (HasPersonaFontSet()) {
    return AllowFontByFamilyNameForPersona(
        family_name, default_shields_settings_->persona_fonts);
  }
  if (!settings->IsReduceLanguageEnabled()) {
    return true;
  }
  switch (default_shields_settings_->farbling_level) {
    case BraveFarblingLevel::OFF:
      return true;
    case BraveFarblingLevel::BALANCED:
    case BraveFarblingLevel::MAXIMUM: {
      if (AllowFontByFamilyName(
              family_name,
              blink::DefaultLanguage().GetString().DeprecatedSubstring(0, 2))) {
        return true;
      }
      if (IsFontAllowedForFarbling(family_name)) {
        FarblingPRNG prng = MakePseudoRandomGenerator();
        prng.discard(family_name.Impl()->GetHash() % 16);
        return ((prng() % 20) == 0);
      } else {
        return false;
      }
    }
  }
  NOTREACHED();
}

brave_shields::FarblingPRNG BraveSessionCache::MakePseudoRandomGenerator(
    FarbleKey key) {
  uint64_t seed = default_shields_settings_->farbling_token.high() ^
                  default_shields_settings_->farbling_token.low() ^
                  static_cast<uint64_t>(key);
  return FarblingPRNG(seed);
}

BraveFarblingLevel BraveSessionCache::GetBraveFarblingLevel(
    ContentSettingsType webcompat_content_settings) {
  if (default_shields_settings_->farbling_level == BraveFarblingLevel::OFF) {
    return BraveFarblingLevel::OFF;
  }
  auto item = farbling_levels_.find(webcompat_content_settings);
  if (item != farbling_levels_.end()) {
    return item->value;
  }
  // The farbling level for webcompat_content_settings is not known yet,
  // so we will make a more expensive call to learn what it is.
  if (webcompat_content_settings > ContentSettingsType::BRAVE_WEBCOMPAT_NONE &&
      webcompat_content_settings < ContentSettingsType::BRAVE_WEBCOMPAT_ALL) {
    if (auto* settings_client =
            GetContentSettingsClientFor(GetSupplementable())) {
      auto shields_settings =
          settings_client->GetBraveShieldsSettings(webcompat_content_settings);
      // https://github.com/brave/brave-browser/issues/41889 debug.
      if (!shields_settings) {
        DEBUG_ALIAS_FOR_OBJECT(settings_client_alias, settings_client);
        base::debug::DumpWithoutCrashing();
        return default_shields_settings_->farbling_level;
      }
      farbling_levels_.insert(webcompat_content_settings,
                              shields_settings->farbling_level);
      return shields_settings->farbling_level;
    }
  }
  return default_shields_settings_->farbling_level;
}

}  // namespace brave
