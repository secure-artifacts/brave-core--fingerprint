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
  const auto persisted = PersonaFromValue(prefs_->GetDict(prefs::kPersona));
  if (persisted) {
    persona_ = *persisted;
    last_error_.clear();
    return true;
  }

  std::string error;
  auto generated =
      GeneratePersonaFromSeed(GetDefaultTruthPool(), profile_seed_, &error);
  if (!generated) {
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
