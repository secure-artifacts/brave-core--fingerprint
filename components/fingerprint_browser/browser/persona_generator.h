/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_FINGERPRINT_BROWSER_BROWSER_PERSONA_GENERATOR_H_
#define BRAVE_COMPONENTS_FINGERPRINT_BROWSER_BROWSER_PERSONA_GENERATOR_H_

#include <optional>
#include <string>
#include <string_view>

#include "brave/components/fingerprint_browser/browser/persona.h"
#include "brave/components/fingerprint_browser/browser/truth_pool.h"

namespace fingerprint_browser {

std::optional<Persona> GeneratePersonaFromSeed(const TruthPool& pool,
                                               std::string_view seed,
                                               std::string* error);

}  // namespace fingerprint_browser

#endif  // BRAVE_COMPONENTS_FINGERPRINT_BROWSER_BROWSER_PERSONA_GENERATOR_H_
