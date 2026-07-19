/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/fingerprint_browser/fingerprint_proxy_service_factory.h"

#include "base/no_destructor.h"
#include "brave/browser/fingerprint_browser/fingerprint_proxy_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_selections.h"
#include "content/public/browser/browser_context.h"

namespace fingerprint_browser {

FingerprintProxyServiceFactory* FingerprintProxyServiceFactory::GetInstance() {
  static base::NoDestructor<FingerprintProxyServiceFactory> instance;
  return instance.get();
}

FingerprintProxyService* FingerprintProxyServiceFactory::GetForProfile(
    Profile* profile) {
  if (!profile || profile->IsGuestSession() || profile->IsTor()) {
    return nullptr;
  }
  return static_cast<FingerprintProxyService*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

FingerprintProxyServiceFactory::FingerprintProxyServiceFactory()
    : ProfileKeyedServiceFactory(
          "FingerprintProxyService",
          ProfileSelections::BuildRedirectedInIncognito()) {}

FingerprintProxyServiceFactory::~FingerprintProxyServiceFactory() = default;

std::unique_ptr<KeyedService>
FingerprintProxyServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  return std::make_unique<FingerprintProxyService>(
      Profile::FromBrowserContext(context));
}

bool FingerprintProxyServiceFactory::ServiceIsCreatedWithBrowserContext()
    const {
  return true;
}

}  // namespace fingerprint_browser
