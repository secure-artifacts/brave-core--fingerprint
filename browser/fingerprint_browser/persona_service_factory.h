/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_FINGERPRINT_BROWSER_PERSONA_SERVICE_FACTORY_H_
#define BRAVE_BROWSER_FINGERPRINT_BROWSER_PERSONA_SERVICE_FACTORY_H_

#include <memory>
#include <string>
#include <vector>

#include "base/no_destructor.h"
#include "base/token.h"
#include "brave/components/fingerprint_browser/browser/persona.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"
#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"

class Profile;

namespace content {
class BrowserContext;
}  // namespace content

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace fingerprint_browser {

class PersonaService;

class PersonaServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static PersonaServiceFactory* GetInstance();
  static PersonaService* GetForProfile(Profile* profile);

  PersonaServiceFactory(const PersonaServiceFactory&) = delete;
  PersonaServiceFactory& operator=(const PersonaServiceFactory&) = delete;

 private:
  friend base::NoDestructor<PersonaServiceFactory>;

  PersonaServiceFactory();
  ~PersonaServiceFactory() override;

  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  void RegisterProfilePrefs(
      user_prefs::PrefRegistrySyncable* registry) override;
  bool ServiceIsCreatedWithBrowserContext() const override;
};

base::Token GetPersonaFarblingTokenForProfile(Profile* profile);
base::Token GetPersonaFarblingTokenForBrowserContext(
    content::BrowserContext* context);
const Persona* GetPersonaForProfile(Profile* profile);
const Persona* GetPersonaForBrowserContext(content::BrowserContext* context);
std::vector<std::string> UserAgentBrandNames(
    const std::vector<UserAgentBrand>& brands);
std::vector<std::string> UserAgentBrandVersions(
    const std::vector<UserAgentBrand>& brands);
blink::UserAgentMetadata ToBlinkUserAgentMetadata(
    const UserAgentMetadata& metadata);

}  // namespace fingerprint_browser

#endif  // BRAVE_BROWSER_FINGERPRINT_BROWSER_PERSONA_SERVICE_FACTORY_H_
