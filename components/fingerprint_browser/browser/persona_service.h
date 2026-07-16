/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_FINGERPRINT_BROWSER_BROWSER_PERSONA_SERVICE_H_
#define BRAVE_COMPONENTS_FINGERPRINT_BROWSER_BROWSER_PERSONA_SERVICE_H_

#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/token.h"
#include "brave/components/fingerprint_browser/browser/persona.h"
#include "components/keyed_service/core/keyed_service.h"

class PrefRegistrySimple;
class PrefService;

namespace fingerprint_browser {

class PersonaService : public KeyedService {
 public:
  PersonaService(PrefService* prefs, std::string profile_seed);
  ~PersonaService() override;

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  bool EnsurePersona();
  bool has_persona() const { return persona_.has_value(); }
  const Persona& GetPersona() const;
  base::Token GetFarblingToken() const;
  const std::string& last_error() const { return last_error_; }

 private:
  raw_ptr<PrefService> prefs_;
  std::string profile_seed_;
  std::optional<Persona> persona_;
  std::string last_error_;
};

}  // namespace fingerprint_browser

#endif  // BRAVE_COMPONENTS_FINGERPRINT_BROWSER_BROWSER_PERSONA_SERVICE_H_
