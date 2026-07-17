/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/third_party/blink/renderer/core/farbling/brave_session_cache.h"

#include <algorithm>
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

void BraveSessionCache::FarbleAudioChannel(base::span<float> dst) {
  const auto& audio_farbling_helper = GetAudioFarblingHelper();
  if (audio_farbling_helper) {
    audio_farbling_helper->FarbleAudioChannel(dst);
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
