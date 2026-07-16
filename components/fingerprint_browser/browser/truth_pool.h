/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_FINGERPRINT_BROWSER_BROWSER_TRUTH_POOL_H_
#define BRAVE_COMPONENTS_FINGERPRINT_BROWSER_BROWSER_TRUTH_POOL_H_

#include <string>
#include <vector>

#include "brave/components/fingerprint_browser/browser/persona.h"

namespace fingerprint_browser {

struct UserAgentPoolEntry {
  std::string id;
  PersonaOS os = PersonaOS::kWindows;
  std::string user_agent;
  UserAgentMetadata metadata;
  std::string platform;
};

struct RendererPoolEntry {
  std::string id;
  PersonaOS os = PersonaOS::kWindows;
  WebGLPersona webgl;
  WebGPUPersona webgpu;
};

struct ScreenPoolEntry {
  std::string id;
  PersonaOS os = PersonaOS::kWindows;
  ScreenPersona screen;
  int hardware_concurrency = 0;
  double device_memory_gb = 0.0;
  int max_touch_points = 0;
};

struct FontPoolEntry {
  std::string id;
  PersonaOS os = PersonaOS::kWindows;
  std::string locale;
  std::vector<std::string> fonts;
};

struct LocalePoolEntry {
  std::string id;
  std::string locale;
  std::vector<std::string> languages;
  std::string accept_language;
};

struct NoiseSeedPoolEntry {
  std::string id;
  std::string canvas_noise_seed;
  std::string audio_noise_seed;
};

struct MediaDevicePoolEntry {
  std::string id;
  PersonaOS os = PersonaOS::kWindows;
  std::vector<MediaDevicePersona> devices;
};

struct SpeechVoicePoolEntry {
  std::string id;
  PersonaOS os = PersonaOS::kWindows;
  std::string locale;
  std::vector<SpeechVoicePersona> voices;
};

struct TruthPool {
  std::vector<UserAgentPoolEntry> user_agents;
  std::vector<RendererPoolEntry> renderers;
  std::vector<ScreenPoolEntry> screens;
  std::vector<FontPoolEntry> font_sets;
  std::vector<LocalePoolEntry> locales;
  std::vector<NoiseSeedPoolEntry> noise_seeds;
  std::vector<MediaDevicePoolEntry> media_device_sets;
  std::vector<SpeechVoicePoolEntry> speech_voice_sets;
};

TruthPool GetDefaultTruthPool();

}  // namespace fingerprint_browser

#endif  // BRAVE_COMPONENTS_FINGERPRINT_BROWSER_BROWSER_TRUTH_POOL_H_
