/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/fingerprint_browser/browser/persona.h"

#include <utility>

#include "base/strings/string_util.h"

namespace fingerprint_browser {

namespace {

constexpr char kSchemaVersionKey[] = "schema_version";
constexpr char kPersonaIdKey[] = "persona_id";
constexpr char kOSKey[] = "os";
constexpr char kLocaleKey[] = "locale";
constexpr char kLanguagesKey[] = "languages";
constexpr char kAcceptLanguageKey[] = "accept_language";
constexpr char kUserAgentKey[] = "user_agent";
constexpr char kUserAgentMetadataKey[] = "ua_metadata";
constexpr char kPlatformKey[] = "platform";
constexpr char kHardwareConcurrencyKey[] = "hardware_concurrency";
constexpr char kDeviceMemoryKey[] = "device_memory_gb";
constexpr char kMaxTouchPointsKey[] = "max_touch_points";
constexpr char kScreenKey[] = "screen";
constexpr char kWebGLKey[] = "webgl";
constexpr char kWebGPUKey[] = "webgpu";
constexpr char kCanvasNoiseSeedKey[] = "canvas_noise_seed";
constexpr char kAudioNoiseSeedKey[] = "audio_noise_seed";
constexpr char kFontsKey[] = "fonts";
constexpr char kMediaDevicesKey[] = "media_devices";
constexpr char kSpeechVoicesKey[] = "speech_voices";

constexpr char kBrandKey[] = "brand";
constexpr char kVersionKey[] = "version";
constexpr char kBrandsKey[] = "brands";
constexpr char kFullVersionListKey[] = "full_version_list";
constexpr char kPlatformVersionKey[] = "platform_version";
constexpr char kArchitectureKey[] = "architecture";
constexpr char kBitnessKey[] = "bitness";
constexpr char kFullVersionKey[] = "full_version";
constexpr char kMobileKey[] = "mobile";

constexpr char kWidthKey[] = "width";
constexpr char kHeightKey[] = "height";
constexpr char kAvailWidthKey[] = "avail_width";
constexpr char kAvailHeightKey[] = "avail_height";
constexpr char kColorDepthKey[] = "color_depth";
constexpr char kDeviceScaleFactorKey[] = "device_scale_factor";
constexpr char kWindowXKey[] = "window_x";
constexpr char kWindowYKey[] = "window_y";

constexpr char kVendorKey[] = "vendor";
constexpr char kRendererKey[] = "renderer";
constexpr char kDeviceKey[] = "device";
constexpr char kDescriptionKey[] = "description";

constexpr char kKindKey[] = "kind";
constexpr char kDeviceIdKey[] = "device_id";
constexpr char kLabelKey[] = "label";
constexpr char kGroupIdKey[] = "group_id";

constexpr char kVoiceUriKey[] = "voice_uri";
constexpr char kNameKey[] = "name";
constexpr char kLangKey[] = "lang";
constexpr char kLocalServiceKey[] = "local_service";
constexpr char kIsDefaultKey[] = "is_default";

base::ListValue StringListToValue(const std::vector<std::string>& strings) {
  base::ListValue list;
  for (const auto& string : strings) {
    list.Append(string);
  }
  return list;
}

std::optional<std::vector<std::string>> StringListFromValue(
    const base::DictValue& dict,
    std::string_view key) {
  const auto* list = dict.FindList(key);
  if (!list) {
    return std::nullopt;
  }

  std::vector<std::string> strings;
  strings.reserve(list->size());
  for (const auto& value : *list) {
    if (!value.is_string() || value.GetString().empty()) {
      return std::nullopt;
    }
    strings.push_back(value.GetString());
  }
  return strings;
}

base::ListValue BrandsToValue(const std::vector<UserAgentBrand>& brands) {
  base::ListValue list;
  for (const auto& brand : brands) {
    base::DictValue value;
    value.Set(kBrandKey, brand.brand);
    value.Set(kVersionKey, brand.version);
    list.Append(std::move(value));
  }
  return list;
}

std::optional<std::vector<UserAgentBrand>> BrandsFromValue(
    const base::DictValue& dict,
    std::string_view key) {
  const auto* list = dict.FindList(key);
  if (!list) {
    return std::nullopt;
  }

  std::vector<UserAgentBrand> brands;
  brands.reserve(list->size());
  for (const auto& value : *list) {
    if (!value.is_dict()) {
      return std::nullopt;
    }
    const auto& brand_dict = value.GetDict();
    const auto* brand = brand_dict.FindString(kBrandKey);
    const auto* version = brand_dict.FindString(kVersionKey);
    if (!brand || brand->empty() || !version || version->empty()) {
      return std::nullopt;
    }
    brands.push_back({*brand, *version});
  }
  return brands;
}

base::DictValue UserAgentMetadataToValue(const UserAgentMetadata& metadata) {
  base::DictValue value;
  value.Set(kPlatformKey, metadata.platform);
  value.Set(kPlatformVersionKey, metadata.platform_version);
  value.Set(kArchitectureKey, metadata.architecture);
  value.Set(kBitnessKey, metadata.bitness);
  value.Set(kFullVersionKey, metadata.full_version);
  value.Set(kBrandsKey, BrandsToValue(metadata.brands));
  value.Set(kFullVersionListKey, BrandsToValue(metadata.full_version_list));
  value.Set(kMobileKey, metadata.mobile);
  return value;
}

std::optional<UserAgentMetadata> UserAgentMetadataFromValue(
    const base::DictValue& value) {
  UserAgentMetadata metadata;
  const auto* platform = value.FindString(kPlatformKey);
  const auto* platform_version = value.FindString(kPlatformVersionKey);
  const auto* architecture = value.FindString(kArchitectureKey);
  const auto* bitness = value.FindString(kBitnessKey);
  const auto* full_version = value.FindString(kFullVersionKey);
  auto brands = BrandsFromValue(value, kBrandsKey);
  auto full_version_list = BrandsFromValue(value, kFullVersionListKey);
  const auto mobile = value.FindBool(kMobileKey);

  if (!platform || platform->empty() || !platform_version ||
      platform_version->empty() || !architecture || architecture->empty() ||
      !bitness || bitness->empty() || !full_version || full_version->empty() ||
      !brands || brands->empty() || !full_version_list ||
      full_version_list->empty() || !mobile) {
    return std::nullopt;
  }

  metadata.platform = *platform;
  metadata.platform_version = *platform_version;
  metadata.architecture = *architecture;
  metadata.bitness = *bitness;
  metadata.full_version = *full_version;
  metadata.brands = std::move(*brands);
  metadata.full_version_list = std::move(*full_version_list);
  metadata.mobile = *mobile;
  return metadata;
}

base::DictValue ScreenToValue(const ScreenPersona& screen) {
  base::DictValue value;
  value.Set(kWidthKey, screen.width);
  value.Set(kHeightKey, screen.height);
  value.Set(kAvailWidthKey, screen.avail_width);
  value.Set(kAvailHeightKey, screen.avail_height);
  value.Set(kColorDepthKey, screen.color_depth);
  value.Set(kDeviceScaleFactorKey, screen.device_scale_factor);
  value.Set(kWindowXKey, screen.window_x);
  value.Set(kWindowYKey, screen.window_y);
  return value;
}

std::optional<ScreenPersona> ScreenFromValue(const base::DictValue& value) {
  ScreenPersona screen;
  const auto width = value.FindInt(kWidthKey);
  const auto height = value.FindInt(kHeightKey);
  const auto avail_width = value.FindInt(kAvailWidthKey);
  const auto avail_height = value.FindInt(kAvailHeightKey);
  const auto color_depth = value.FindInt(kColorDepthKey);
  const auto device_scale_factor = value.FindDouble(kDeviceScaleFactorKey);
  const auto window_x = value.FindInt(kWindowXKey);
  const auto window_y = value.FindInt(kWindowYKey);

  if (!width || !height || !avail_width || !avail_height || !color_depth ||
      !device_scale_factor || !window_x || !window_y) {
    return std::nullopt;
  }

  screen.width = *width;
  screen.height = *height;
  screen.avail_width = *avail_width;
  screen.avail_height = *avail_height;
  screen.color_depth = *color_depth;
  screen.device_scale_factor = *device_scale_factor;
  screen.window_x = *window_x;
  screen.window_y = *window_y;
  return screen;
}

base::DictValue WebGLToValue(const WebGLPersona& webgl) {
  base::DictValue value;
  value.Set(kVendorKey, webgl.vendor);
  value.Set(kRendererKey, webgl.renderer);
  return value;
}

std::optional<WebGLPersona> WebGLFromValue(const base::DictValue& value) {
  const auto* vendor = value.FindString(kVendorKey);
  const auto* renderer = value.FindString(kRendererKey);
  if (!vendor || vendor->empty() || !renderer || renderer->empty()) {
    return std::nullopt;
  }
  return WebGLPersona{*vendor, *renderer};
}

base::DictValue WebGPUToValue(const WebGPUPersona& webgpu) {
  base::DictValue value;
  value.Set(kVendorKey, webgpu.vendor);
  value.Set(kArchitectureKey, webgpu.architecture);
  value.Set(kDeviceKey, webgpu.device);
  value.Set(kDescriptionKey, webgpu.description);
  return value;
}

std::optional<WebGPUPersona> WebGPUFromValue(const base::DictValue& value) {
  const auto* vendor = value.FindString(kVendorKey);
  const auto* architecture = value.FindString(kArchitectureKey);
  const auto* device = value.FindString(kDeviceKey);
  const auto* description = value.FindString(kDescriptionKey);
  if (!vendor || vendor->empty() || !architecture || architecture->empty() ||
      !device || device->empty() || !description || description->empty()) {
    return std::nullopt;
  }
  return WebGPUPersona{*vendor, *architecture, *device, *description};
}

base::ListValue MediaDevicesToValue(
    const std::vector<MediaDevicePersona>& devices) {
  base::ListValue list;
  for (const auto& device : devices) {
    base::DictValue value;
    value.Set(kKindKey, PersonaMediaDeviceKindToString(device.kind));
    value.Set(kDeviceIdKey, device.device_id);
    value.Set(kLabelKey, device.label);
    value.Set(kGroupIdKey, device.group_id);
    list.Append(std::move(value));
  }
  return list;
}

std::optional<std::vector<MediaDevicePersona>> MediaDevicesFromValue(
    const base::DictValue& dict,
    std::string_view key) {
  const auto* list = dict.FindList(key);
  if (!list) {
    return std::nullopt;
  }

  std::vector<MediaDevicePersona> devices;
  devices.reserve(list->size());
  for (const auto& value : *list) {
    if (!value.is_dict()) {
      return std::nullopt;
    }
    const auto& device_dict = value.GetDict();
    const auto* kind = device_dict.FindString(kKindKey);
    const auto* device_id = device_dict.FindString(kDeviceIdKey);
    const auto* label = device_dict.FindString(kLabelKey);
    const auto* group_id = device_dict.FindString(kGroupIdKey);
    if (!kind || !device_id || device_id->empty() || !label || label->empty() ||
        !group_id || group_id->empty()) {
      return std::nullopt;
    }
    auto parsed_kind = PersonaMediaDeviceKindFromString(*kind);
    if (!parsed_kind) {
      return std::nullopt;
    }
    devices.push_back({*parsed_kind, *device_id, *label, *group_id});
  }
  return devices;
}

base::ListValue SpeechVoicesToValue(
    const std::vector<SpeechVoicePersona>& voices) {
  base::ListValue list;
  for (const auto& voice : voices) {
    base::DictValue value;
    value.Set(kVoiceUriKey, voice.voice_uri);
    value.Set(kNameKey, voice.name);
    value.Set(kLangKey, voice.lang);
    value.Set(kLocalServiceKey, voice.local_service);
    value.Set(kIsDefaultKey, voice.is_default);
    list.Append(std::move(value));
  }
  return list;
}

std::optional<std::vector<SpeechVoicePersona>> SpeechVoicesFromValue(
    const base::DictValue& dict,
    std::string_view key) {
  const auto* list = dict.FindList(key);
  if (!list) {
    return std::nullopt;
  }

  std::vector<SpeechVoicePersona> voices;
  voices.reserve(list->size());
  for (const auto& value : *list) {
    if (!value.is_dict()) {
      return std::nullopt;
    }
    const auto& voice_dict = value.GetDict();
    const auto* voice_uri = voice_dict.FindString(kVoiceUriKey);
    const auto* name = voice_dict.FindString(kNameKey);
    const auto* lang = voice_dict.FindString(kLangKey);
    const auto local_service = voice_dict.FindBool(kLocalServiceKey);
    const auto is_default = voice_dict.FindBool(kIsDefaultKey);
    if (!voice_uri || voice_uri->empty() || !name || name->empty() || !lang ||
        lang->empty() || !local_service || !is_default) {
      return std::nullopt;
    }
    voices.push_back({*voice_uri, *name, *lang, *local_service, *is_default});
  }
  return voices;
}

bool ContainsString(std::string_view haystack, std::string_view needle) {
  return haystack.find(needle) != std::string_view::npos;
}

}  // namespace

std::string_view PersonaOSToString(PersonaOS os) {
  switch (os) {
    case PersonaOS::kWindows:
      return "windows";
    case PersonaOS::kMacOS:
      return "macos";
  }
}

std::optional<PersonaOS> PersonaOSFromString(std::string_view value) {
  if (value == "windows") {
    return PersonaOS::kWindows;
  }
  if (value == "macos") {
    return PersonaOS::kMacOS;
  }
  return std::nullopt;
}

std::string_view PersonaMediaDeviceKindToString(PersonaMediaDeviceKind kind) {
  switch (kind) {
    case PersonaMediaDeviceKind::kAudioInput:
      return "audioinput";
    case PersonaMediaDeviceKind::kVideoInput:
      return "videoinput";
    case PersonaMediaDeviceKind::kAudioOutput:
      return "audiooutput";
  }
}

std::optional<PersonaMediaDeviceKind> PersonaMediaDeviceKindFromString(
    std::string_view value) {
  if (value == "audioinput") {
    return PersonaMediaDeviceKind::kAudioInput;
  }
  if (value == "videoinput") {
    return PersonaMediaDeviceKind::kVideoInput;
  }
  if (value == "audiooutput") {
    return PersonaMediaDeviceKind::kAudioOutput;
  }
  return std::nullopt;
}

base::DictValue PersonaToValue(const Persona& persona) {
  base::DictValue value;
  value.Set(kSchemaVersionKey, persona.schema_version);
  value.Set(kPersonaIdKey, persona.persona_id);
  value.Set(kOSKey, PersonaOSToString(persona.os));
  value.Set(kLocaleKey, persona.locale);
  value.Set(kLanguagesKey, StringListToValue(persona.languages));
  value.Set(kAcceptLanguageKey, persona.accept_language);
  value.Set(kUserAgentKey, persona.user_agent);
  value.Set(kUserAgentMetadataKey,
            UserAgentMetadataToValue(persona.ua_metadata));
  value.Set(kPlatformKey, persona.platform);
  value.Set(kHardwareConcurrencyKey, persona.hardware_concurrency);
  value.Set(kDeviceMemoryKey, persona.device_memory_gb);
  value.Set(kMaxTouchPointsKey, persona.max_touch_points);
  value.Set(kScreenKey, ScreenToValue(persona.screen));
  value.Set(kWebGLKey, WebGLToValue(persona.webgl));
  value.Set(kWebGPUKey, WebGPUToValue(persona.webgpu));
  value.Set(kCanvasNoiseSeedKey, persona.canvas_noise_seed);
  value.Set(kAudioNoiseSeedKey, persona.audio_noise_seed);
  value.Set(kFontsKey, StringListToValue(persona.fonts));
  value.Set(kMediaDevicesKey, MediaDevicesToValue(persona.media_devices));
  value.Set(kSpeechVoicesKey, SpeechVoicesToValue(persona.speech_voices));
  return value;
}

std::optional<Persona> PersonaFromValue(const base::DictValue& value) {
  Persona persona;
  const auto schema_version = value.FindInt(kSchemaVersionKey);
  if (!schema_version || *schema_version != kCurrentPersonaSchemaVersion) {
    return std::nullopt;
  }
  persona.schema_version = *schema_version;

  const auto* persona_id = value.FindString(kPersonaIdKey);
  const auto* os = value.FindString(kOSKey);
  const auto* locale = value.FindString(kLocaleKey);
  auto languages = StringListFromValue(value, kLanguagesKey);
  const auto* accept_language = value.FindString(kAcceptLanguageKey);
  const auto* user_agent = value.FindString(kUserAgentKey);
  const auto* ua_metadata_dict = value.FindDict(kUserAgentMetadataKey);
  const auto* platform = value.FindString(kPlatformKey);
  const auto hardware_concurrency = value.FindInt(kHardwareConcurrencyKey);
  const auto device_memory = value.FindDouble(kDeviceMemoryKey);
  const auto max_touch_points = value.FindInt(kMaxTouchPointsKey);
  const auto* screen_dict = value.FindDict(kScreenKey);
  const auto* webgl_dict = value.FindDict(kWebGLKey);
  const auto* webgpu_dict = value.FindDict(kWebGPUKey);
  const auto* canvas_noise_seed = value.FindString(kCanvasNoiseSeedKey);
  const auto* audio_noise_seed = value.FindString(kAudioNoiseSeedKey);
  auto fonts = StringListFromValue(value, kFontsKey);
  auto media_devices = MediaDevicesFromValue(value, kMediaDevicesKey);
  auto speech_voices = SpeechVoicesFromValue(value, kSpeechVoicesKey);

  if (!persona_id || persona_id->empty() || !os || os->empty() || !locale ||
      locale->empty() || !languages || !accept_language ||
      accept_language->empty() || !user_agent || user_agent->empty() ||
      !ua_metadata_dict || !platform || platform->empty() ||
      !hardware_concurrency || !device_memory || !max_touch_points ||
      !screen_dict || !webgl_dict || !webgpu_dict || !canvas_noise_seed ||
      canvas_noise_seed->empty() || !audio_noise_seed ||
      audio_noise_seed->empty() || !fonts || !media_devices || !speech_voices) {
    return std::nullopt;
  }

  auto parsed_os = PersonaOSFromString(*os);
  auto ua_metadata = UserAgentMetadataFromValue(*ua_metadata_dict);
  auto screen = ScreenFromValue(*screen_dict);
  auto webgl = WebGLFromValue(*webgl_dict);
  auto webgpu = WebGPUFromValue(*webgpu_dict);
  if (!parsed_os || !ua_metadata || !screen || !webgl || !webgpu) {
    return std::nullopt;
  }

  persona.persona_id = *persona_id;
  persona.os = *parsed_os;
  persona.locale = *locale;
  persona.languages = std::move(*languages);
  persona.accept_language = *accept_language;
  persona.user_agent = *user_agent;
  persona.ua_metadata = std::move(*ua_metadata);
  persona.platform = *platform;
  persona.hardware_concurrency = *hardware_concurrency;
  persona.device_memory_gb = *device_memory;
  persona.max_touch_points = *max_touch_points;
  persona.screen = *screen;
  persona.webgl = *webgl;
  persona.webgpu = *webgpu;
  persona.canvas_noise_seed = *canvas_noise_seed;
  persona.audio_noise_seed = *audio_noise_seed;
  persona.fonts = std::move(*fonts);
  persona.media_devices = std::move(*media_devices);
  persona.speech_voices = std::move(*speech_voices);

  if (!IsPersonaValid(persona)) {
    return std::nullopt;
  }
  return persona;
}

bool IsPersonaValid(const Persona& persona) {
  if (persona.schema_version != kCurrentPersonaSchemaVersion ||
      persona.persona_id.empty() || persona.locale.empty() ||
      persona.languages.empty() || persona.accept_language.empty() ||
      persona.user_agent.empty() || persona.ua_metadata.platform.empty() ||
      persona.ua_metadata.platform_version.empty() ||
      persona.ua_metadata.architecture.empty() ||
      persona.ua_metadata.bitness.empty() ||
      persona.ua_metadata.full_version.empty() ||
      persona.ua_metadata.brands.empty() ||
      persona.ua_metadata.full_version_list.empty() ||
      persona.platform.empty() || persona.hardware_concurrency <= 0 ||
      persona.device_memory_gb <= 0 || persona.max_touch_points < 0 ||
      persona.screen.width <= 0 || persona.screen.height <= 0 ||
      persona.screen.avail_width <= 0 || persona.screen.avail_height <= 0 ||
      persona.screen.color_depth <= 0 ||
      persona.screen.device_scale_factor <= 0 || persona.webgl.vendor.empty() ||
      persona.webgl.renderer.empty() || persona.webgpu.vendor.empty() ||
      persona.webgpu.architecture.empty() || persona.webgpu.device.empty() ||
      persona.webgpu.description.empty() || persona.canvas_noise_seed.empty() ||
      persona.audio_noise_seed.empty() || persona.fonts.empty() ||
      persona.media_devices.empty() || persona.speech_voices.empty()) {
    return false;
  }

  if (persona.screen.avail_width > persona.screen.width ||
      persona.screen.avail_height > persona.screen.height) {
    return false;
  }

  if (persona.max_touch_points != 0) {
    return false;
  }

  for (const auto& device : persona.media_devices) {
    if (device.device_id.empty() || device.label.empty() ||
        device.group_id.empty()) {
      return false;
    }
  }

  int default_voice_count = 0;
  for (const auto& voice : persona.speech_voices) {
    if (voice.voice_uri.empty() || voice.name.empty() || voice.lang.empty()) {
      return false;
    }
    if (voice.is_default) {
      ++default_voice_count;
    }
  }
  if (default_voice_count != 1) {
    return false;
  }

  switch (persona.os) {
    case PersonaOS::kWindows:
      return ContainsString(persona.user_agent, "Windows") &&
             persona.ua_metadata.platform == "Windows" &&
             base::StartsWith(persona.platform, "Win",
                              base::CompareCase::SENSITIVE) &&
             ContainsString(persona.webgl.renderer, "Direct3D");
    case PersonaOS::kMacOS:
      return ContainsString(persona.user_agent, "Macintosh") &&
             persona.ua_metadata.platform == "macOS" &&
             persona.platform == "MacIntel" &&
             ContainsString(persona.webgl.renderer, "Metal");
  }
}

std::vector<std::string> PersonaMediaDeviceKinds(
    const std::vector<MediaDevicePersona>& devices) {
  std::vector<std::string> result;
  result.reserve(devices.size());
  for (const auto& device : devices) {
    result.emplace_back(PersonaMediaDeviceKindToString(device.kind));
  }
  return result;
}

std::vector<std::string> PersonaMediaDeviceIds(
    const std::vector<MediaDevicePersona>& devices) {
  std::vector<std::string> result;
  result.reserve(devices.size());
  for (const auto& device : devices) {
    result.push_back(device.device_id);
  }
  return result;
}

std::vector<std::string> PersonaMediaDeviceLabels(
    const std::vector<MediaDevicePersona>& devices) {
  std::vector<std::string> result;
  result.reserve(devices.size());
  for (const auto& device : devices) {
    result.push_back(device.label);
  }
  return result;
}

std::vector<std::string> PersonaMediaDeviceGroupIds(
    const std::vector<MediaDevicePersona>& devices) {
  std::vector<std::string> result;
  result.reserve(devices.size());
  for (const auto& device : devices) {
    result.push_back(device.group_id);
  }
  return result;
}

std::vector<std::string> PersonaSpeechVoiceUris(
    const std::vector<SpeechVoicePersona>& voices) {
  std::vector<std::string> result;
  result.reserve(voices.size());
  for (const auto& voice : voices) {
    result.push_back(voice.voice_uri);
  }
  return result;
}

std::vector<std::string> PersonaSpeechVoiceNames(
    const std::vector<SpeechVoicePersona>& voices) {
  std::vector<std::string> result;
  result.reserve(voices.size());
  for (const auto& voice : voices) {
    result.push_back(voice.name);
  }
  return result;
}

std::vector<std::string> PersonaSpeechVoiceLangs(
    const std::vector<SpeechVoicePersona>& voices) {
  std::vector<std::string> result;
  result.reserve(voices.size());
  for (const auto& voice : voices) {
    result.push_back(voice.lang);
  }
  return result;
}

std::vector<bool> PersonaSpeechVoiceLocalServices(
    const std::vector<SpeechVoicePersona>& voices) {
  std::vector<bool> result;
  result.reserve(voices.size());
  for (const auto& voice : voices) {
    result.push_back(voice.local_service);
  }
  return result;
}

std::vector<bool> PersonaSpeechVoiceDefaults(
    const std::vector<SpeechVoicePersona>& voices) {
  std::vector<bool> result;
  result.reserve(voices.size());
  for (const auto& voice : voices) {
    result.push_back(voice.is_default);
  }
  return result;
}

}  // namespace fingerprint_browser
