/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/fingerprint_browser/persona_service_factory.h"

#include "base/no_destructor.h"
#include "brave/components/fingerprint_browser/browser/persona_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_selections.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "content/public/browser/browser_context.h"
#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"

namespace fingerprint_browser {

PersonaServiceFactory* PersonaServiceFactory::GetInstance() {
  static base::NoDestructor<PersonaServiceFactory> instance;
  return instance.get();
}

PersonaService* PersonaServiceFactory::GetForProfile(Profile* profile) {
  return static_cast<PersonaService*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

PersonaServiceFactory::PersonaServiceFactory()
    : ProfileKeyedServiceFactory(
          "FingerprintBrowserPersonaService",
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOwnInstance)
              .WithGuest(ProfileSelection::kOwnInstance)
              .WithSystem(ProfileSelection::kNone)
              .Build()) {}

PersonaServiceFactory::~PersonaServiceFactory() = default;

std::unique_ptr<KeyedService>
PersonaServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  auto* profile = Profile::FromBrowserContext(context);
  return std::make_unique<PersonaService>(profile->GetPrefs(),
                                          profile->GetPath().AsUTF8Unsafe());
}

void PersonaServiceFactory::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  PersonaService::RegisterProfilePrefs(registry);
}

bool PersonaServiceFactory::ServiceIsCreatedWithBrowserContext() const {
  return true;
}

base::Token GetPersonaFarblingTokenForProfile(Profile* profile) {
  const Persona* persona = GetPersonaForProfile(profile);
  if (!persona) {
    return base::Token();
  }

  auto* service = PersonaServiceFactory::GetForProfile(profile);
  return service->GetFarblingToken();
}

base::Token GetPersonaFarblingTokenForBrowserContext(
    content::BrowserContext* context) {
  if (!context) {
    return base::Token();
  }

  return GetPersonaFarblingTokenForProfile(
      Profile::FromBrowserContext(context));
}

const Persona* GetPersonaForProfile(Profile* profile) {
  if (!profile) {
    return nullptr;
  }

  auto* service = PersonaServiceFactory::GetForProfile(profile);
  if (!service || !service->EnsurePersona() || !service->has_persona()) {
    return nullptr;
  }

  return &service->GetPersona();
}

const Persona* GetPersonaForBrowserContext(content::BrowserContext* context) {
  if (!context) {
    return nullptr;
  }

  return GetPersonaForProfile(Profile::FromBrowserContext(context));
}

std::vector<std::string> UserAgentBrandNames(
    const std::vector<UserAgentBrand>& brands) {
  std::vector<std::string> names;
  names.reserve(brands.size());
  for (const auto& brand : brands) {
    names.push_back(brand.brand);
  }
  return names;
}

std::vector<std::string> UserAgentBrandVersions(
    const std::vector<UserAgentBrand>& brands) {
  std::vector<std::string> versions;
  versions.reserve(brands.size());
  for (const auto& brand : brands) {
    versions.push_back(brand.version);
  }
  return versions;
}

blink::UserAgentMetadata ToBlinkUserAgentMetadata(
    const UserAgentMetadata& metadata) {
  blink::UserAgentMetadata blink_metadata;
  for (const auto& brand : metadata.brands) {
    blink_metadata.brand_version_list.emplace_back(brand.brand, brand.version);
  }
  for (const auto& brand : metadata.full_version_list) {
    blink_metadata.brand_full_version_list.emplace_back(brand.brand,
                                                        brand.version);
  }
  blink_metadata.full_version = metadata.full_version;
  blink_metadata.platform = metadata.platform;
  blink_metadata.platform_version = metadata.platform_version;
  blink_metadata.architecture = metadata.architecture;
  blink_metadata.bitness = metadata.bitness;
  blink_metadata.mobile = metadata.mobile;
  blink_metadata.form_factors = {metadata.mobile ? blink::kMobileFormFactor
                                                 : blink::kDesktopFormFactor};
  return blink_metadata;
}

}  // namespace fingerprint_browser
