/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_FINGERPRINT_BROWSER_BROWSER_PERSONA_H_
#define BRAVE_COMPONENTS_FINGERPRINT_BROWSER_BROWSER_PERSONA_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"

namespace fingerprint_browser {

inline constexpr int kCurrentPersonaSchemaVersion = 3;

enum class PersonaOS {
  kWindows,
  kMacOS,
};

struct UserAgentBrand {
  std::string brand;
  std::string version;
};

struct UserAgentMetadata {
  std::string platform;
  std::string platform_version;
  std::string architecture;
  std::string bitness;
  std::string full_version;
  std::vector<UserAgentBrand> brands;
  std::vector<UserAgentBrand> full_version_list;
  bool mobile = false;
};

struct ScreenPersona {
  int width = 0;
  int height = 0;
  int avail_width = 0;
  int avail_height = 0;
  int color_depth = 0;
  double device_scale_factor = 1.0;
  int window_x = 0;
  int window_y = 0;
};

struct WebGLPersona {
  std::string vendor;
  std::string renderer;
};

struct WebGPUPersona {
  std::string vendor;
  std::string architecture;
  std::string device;
  std::string description;
};

enum class PersonaMediaDeviceKind {
  kAudioInput,
  kVideoInput,
  kAudioOutput,
};

struct MediaDevicePersona {
  PersonaMediaDeviceKind kind = PersonaMediaDeviceKind::kAudioInput;
  std::string device_id;
  std::string label;
  std::string group_id;

  bool operator==(const MediaDevicePersona&) const = default;
};

struct SpeechVoicePersona {
  std::string voice_uri;
  std::string name;
  std::string lang;
  bool local_service = true;
  bool is_default = false;

  bool operator==(const SpeechVoicePersona&) const = default;
};

struct MimeTypePersona {
  std::string type;
  std::string description;
  std::vector<std::string> suffixes;

  bool operator==(const MimeTypePersona&) const = default;
};

struct PluginPersona {
  std::string name;
  std::string filename;
  std::string description;
  std::vector<MimeTypePersona> mime_types;

  bool operator==(const PluginPersona&) const = default;
};

struct Persona {
  int schema_version = kCurrentPersonaSchemaVersion;
  std::string persona_id;
  PersonaOS os = PersonaOS::kWindows;
  std::string locale;
  std::vector<std::string> languages;
  std::string accept_language;
  std::string user_agent;
  UserAgentMetadata ua_metadata;
  std::string platform;
  int hardware_concurrency = 0;
  double device_memory_gb = 0.0;
  int max_touch_points = 0;
  ScreenPersona screen;
  WebGLPersona webgl;
  WebGPUPersona webgpu;
  std::string canvas_noise_seed;
  std::string audio_noise_seed;
  std::vector<std::string> fonts;
  std::vector<MediaDevicePersona> media_devices;
  std::vector<SpeechVoicePersona> speech_voices;
  std::vector<PluginPersona> plugins;
};

std::string_view PersonaOSToString(PersonaOS os);
std::optional<PersonaOS> PersonaOSFromString(std::string_view value);
std::string_view PersonaMediaDeviceKindToString(PersonaMediaDeviceKind kind);
std::optional<PersonaMediaDeviceKind> PersonaMediaDeviceKindFromString(
    std::string_view value);

base::DictValue PersonaToValue(const Persona& persona);
std::optional<Persona> PersonaFromValue(const base::DictValue& value);
bool IsPersonaValid(const Persona& persona);

std::vector<std::string> PersonaMediaDeviceKinds(
    const std::vector<MediaDevicePersona>& devices);
std::vector<std::string> PersonaMediaDeviceIds(
    const std::vector<MediaDevicePersona>& devices);
std::vector<std::string> PersonaMediaDeviceLabels(
    const std::vector<MediaDevicePersona>& devices);
std::vector<std::string> PersonaMediaDeviceGroupIds(
    const std::vector<MediaDevicePersona>& devices);
std::vector<std::string> PersonaSpeechVoiceUris(
    const std::vector<SpeechVoicePersona>& voices);
std::vector<std::string> PersonaSpeechVoiceNames(
    const std::vector<SpeechVoicePersona>& voices);
std::vector<std::string> PersonaSpeechVoiceLangs(
    const std::vector<SpeechVoicePersona>& voices);
std::vector<bool> PersonaSpeechVoiceLocalServices(
    const std::vector<SpeechVoicePersona>& voices);
std::vector<bool> PersonaSpeechVoiceDefaults(
    const std::vector<SpeechVoicePersona>& voices);

}  // namespace fingerprint_browser

#endif  // BRAVE_COMPONENTS_FINGERPRINT_BROWSER_BROWSER_PERSONA_H_
