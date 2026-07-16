/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/fingerprint_browser/browser/persona_generator.h"

#include <utility>
#include <vector>

#include "base/hash/hash.h"
#include "base/strings/strcat.h"
#include "base/strings/stringprintf.h"

namespace fingerprint_browser {

namespace {

uint32_t HashFor(std::string_view seed, std::string_view salt) {
  return base::PersistentHash(base::StrCat({seed, ":", salt}));
}

std::string MakePersonaId(std::string_view seed,
                          std::string_view ua_id,
                          std::string_view renderer_id,
                          std::string_view screen_id,
                          std::string_view font_id,
                          std::string_view noise_id,
                          std::string_view media_device_id,
                          std::string_view speech_voice_id) {
  const auto high = HashFor(
      seed, base::StrCat({ua_id, renderer_id, screen_id, media_device_id}));
  const auto low =
      HashFor(seed, base::StrCat({font_id, noise_id, speech_voice_id}));
  return base::StringPrintf("%08x%08x", high, low);
}

template <typename Entry>
std::optional<Entry> PickByHash(const std::vector<Entry>& entries,
                                std::string_view seed,
                                std::string_view salt,
                                std::string* error) {
  if (entries.empty()) {
    if (error) {
      *error = base::StrCat({"truth pool has no candidates for ", salt});
    }
    return std::nullopt;
  }

  return entries[HashFor(seed, salt) % entries.size()];
}

template <typename Entry>
std::vector<Entry> FilterByOS(const std::vector<Entry>& entries, PersonaOS os) {
  std::vector<Entry> filtered;
  for (const auto& entry : entries) {
    if (entry.os == os) {
      filtered.push_back(entry);
    }
  }
  return filtered;
}

std::vector<FontPoolEntry> FilterFonts(
    const std::vector<FontPoolEntry>& entries,
    PersonaOS os,
    std::string_view locale) {
  std::vector<FontPoolEntry> filtered;
  for (const auto& entry : entries) {
    if (entry.os == os && entry.locale == locale) {
      filtered.push_back(entry);
    }
  }
  return filtered;
}

std::vector<LocalePoolEntry> FilterLocales(
    const std::vector<LocalePoolEntry>& entries,
    std::string_view locale) {
  std::vector<LocalePoolEntry> filtered;
  for (const auto& entry : entries) {
    if (entry.locale == locale) {
      filtered.push_back(entry);
    }
  }
  return filtered;
}

std::vector<SpeechVoicePoolEntry> FilterSpeechVoices(
    const std::vector<SpeechVoicePoolEntry>& entries,
    PersonaOS os,
    std::string_view locale) {
  std::vector<SpeechVoicePoolEntry> filtered;
  for (const auto& entry : entries) {
    if (entry.os == os && entry.locale == locale) {
      filtered.push_back(entry);
    }
  }
  return filtered;
}

void SetError(std::string* error, std::string_view value) {
  if (error) {
    *error = std::string(value);
  }
}

}  // namespace

std::optional<Persona> GeneratePersonaFromSeed(const TruthPool& pool,
                                               std::string_view seed,
                                               std::string* error) {
  if (seed.empty()) {
    SetError(error, "persona seed must not be empty");
    return std::nullopt;
  }

  auto user_agent = PickByHash(pool.user_agents, seed, "user_agent", error);
  if (!user_agent) {
    return std::nullopt;
  }

  auto renderers = FilterByOS(pool.renderers, user_agent->os);
  auto renderer = PickByHash(renderers, seed, "renderer", error);
  if (!renderer) {
    return std::nullopt;
  }

  auto screens = FilterByOS(pool.screens, user_agent->os);
  auto screen = PickByHash(screens, seed, "screen", error);
  if (!screen) {
    return std::nullopt;
  }

  const std::string locale = "en-US";
  auto locales = FilterLocales(pool.locales, locale);
  auto locale_entry = PickByHash(locales, seed, "locale", error);
  if (!locale_entry) {
    return std::nullopt;
  }

  auto fonts =
      FilterFonts(pool.font_sets, user_agent->os, locale_entry->locale);
  auto font_set = PickByHash(fonts, seed, "fonts", error);
  if (!font_set) {
    return std::nullopt;
  }

  auto noise_seed = PickByHash(pool.noise_seeds, seed, "noise", error);
  if (!noise_seed) {
    return std::nullopt;
  }

  auto media_device_sets = FilterByOS(pool.media_device_sets, user_agent->os);
  auto media_device_set =
      PickByHash(media_device_sets, seed, "media_devices", error);
  if (!media_device_set) {
    return std::nullopt;
  }

  auto speech_voice_sets = FilterSpeechVoices(
      pool.speech_voice_sets, user_agent->os, locale_entry->locale);
  auto speech_voice_set =
      PickByHash(speech_voice_sets, seed, "speech_voices", error);
  if (!speech_voice_set) {
    return std::nullopt;
  }

  Persona persona;
  persona.persona_id = MakePersonaId(
      seed, user_agent->id, renderer->id, screen->id, font_set->id,
      noise_seed->id, media_device_set->id, speech_voice_set->id);
  persona.os = user_agent->os;
  persona.locale = locale_entry->locale;
  persona.languages = locale_entry->languages;
  persona.accept_language = locale_entry->accept_language;
  persona.user_agent = user_agent->user_agent;
  persona.ua_metadata = user_agent->metadata;
  persona.platform = user_agent->platform;
  persona.hardware_concurrency = screen->hardware_concurrency;
  persona.device_memory_gb = screen->device_memory_gb;
  persona.max_touch_points = screen->max_touch_points;
  persona.screen = screen->screen;
  persona.webgl = renderer->webgl;
  persona.webgpu = renderer->webgpu;
  persona.canvas_noise_seed = noise_seed->canvas_noise_seed;
  persona.audio_noise_seed = noise_seed->audio_noise_seed;
  persona.fonts = font_set->fonts;
  persona.media_devices = media_device_set->devices;
  persona.speech_voices = speech_voice_set->voices;

  if (!IsPersonaValid(persona)) {
    SetError(error, "generated persona failed validation");
    return std::nullopt;
  }
  if (error) {
    error->clear();
  }
  return persona;
}

}  // namespace fingerprint_browser
