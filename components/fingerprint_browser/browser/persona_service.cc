/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/fingerprint_browser/browser/persona_service.h"

#include <cstdint>
#include <utility>

#include "base/check.h"
#include "base/hash/hash.h"
#include "base/strings/strcat.h"
#include "brave/components/fingerprint_browser/browser/persona_generator.h"
#include "brave/components/fingerprint_browser/browser/pref_names.h"
#include "brave/components/fingerprint_browser/browser/truth_pool.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace fingerprint_browser {

namespace {

uint64_t Hash64(std::string_view value, std::string_view salt) {
  const uint32_t high =
      base::PersistentHash(base::StrCat({value, ":", salt, ":high"}));
  const uint32_t low =
      base::PersistentHash(base::StrCat({value, ":", salt, ":low"}));
  return (static_cast<uint64_t>(high) << 32) | low;
}

}  // namespace

PersonaService::PersonaService(PrefService* prefs, std::string profile_seed)
    : prefs_(prefs), profile_seed_(std::move(profile_seed)) {
  DCHECK(prefs_);
  EnsurePersona();
}

PersonaService::~PersonaService() = default;

void PersonaService::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  prefs::RegisterProfilePrefs(registry);
}

bool PersonaService::EnsurePersona() {
  const base::Value* user_value = prefs_->GetUserPrefValue(prefs::kPersona);
  if (user_value) {
    if (!user_value->is_dict()) {
      persona_.reset();
      last_error_ = "persisted persona is not a dictionary";
      return false;
    }

    const base::DictValue& stored = user_value->GetDict();
    const std::optional<int> schema_version = stored.FindInt("schema_version");
    if (schema_version && *schema_version == kCurrentPersonaSchemaVersion) {
      auto persisted = PersonaFromValue(stored);
      if (!persisted) {
        persona_.reset();
        last_error_ = "persisted persona failed validation";
        return false;
      }
      persona_ = std::move(*persisted);
      last_error_.clear();
      return true;
    }

    if (!schema_version || *schema_version <= 0 ||
        *schema_version > kCurrentPersonaSchemaVersion) {
      persona_.reset();
      last_error_ = "persisted persona has an unsupported schema";
      return false;
    }

    std::string error;
    auto defaults =
        GeneratePersonaFromSeed(GetDefaultTruthPool(), profile_seed_, &error);
    if (!defaults) {
      persona_.reset();
      last_error_ = error;
      return false;
    }

    base::DictValue migrated = stored.Clone();
    const base::DictValue default_values = PersonaToValue(*defaults);
    for (const auto [key, value] : default_values) {
      if (!migrated.Find(key)) {
        migrated.Set(key, value.Clone());
      }
    }
    migrated.Set("schema_version", kCurrentPersonaSchemaVersion);

    auto persisted = PersonaFromValue(migrated);
    if (!persisted) {
      persona_.reset();
      last_error_ = "persisted persona migration failed validation";
      return false;
    }

    prefs_->SetDict(prefs::kPersona, std::move(migrated));
    persona_ = std::move(*persisted);
    last_error_.clear();
    return true;
  }

  std::string error;
  auto generated =
      GeneratePersonaFromSeed(GetDefaultTruthPool(), profile_seed_, &error);
  if (!generated) {
    persona_.reset();
    last_error_ = error;
    return false;
  }

  prefs_->SetDict(prefs::kPersona, PersonaToValue(*generated));
  persona_ = std::move(generated);
  last_error_.clear();
  return true;
}

const Persona& PersonaService::GetPersona() const {
  CHECK(persona_);
  return *persona_;
}

base::Token PersonaService::GetFarblingToken() const {
  const Persona& persona = GetPersona();
  const std::string token_seed =
      base::StrCat({persona.persona_id, ":", persona.canvas_noise_seed, ":",
                    persona.audio_noise_seed});
  base::Token token(Hash64(token_seed, "farbling-token-high"),
                    Hash64(token_seed, "farbling-token-low"));
  if (token.is_zero()) {
    return base::Token(1, 1);
  }
  return token;
}

}  // namespace fingerprint_browser
