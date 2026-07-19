/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_FINGERPRINT_BROWSER_FINGERPRINT_PROXY_SERVICE_FACTORY_H_
#define BRAVE_BROWSER_FINGERPRINT_BROWSER_FINGERPRINT_PROXY_SERVICE_FACTORY_H_

#include <memory>

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace content {
class BrowserContext;
}

namespace fingerprint_browser {

class FingerprintProxyService;

class FingerprintProxyServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static FingerprintProxyServiceFactory* GetInstance();
  static FingerprintProxyService* GetForProfile(Profile* profile);

  FingerprintProxyServiceFactory(const FingerprintProxyServiceFactory&) =
      delete;
  FingerprintProxyServiceFactory& operator=(
      const FingerprintProxyServiceFactory&) = delete;

 private:
  friend base::NoDestructor<FingerprintProxyServiceFactory>;

  FingerprintProxyServiceFactory();
  ~FingerprintProxyServiceFactory() override;

  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  bool ServiceIsCreatedWithBrowserContext() const override;
};

}  // namespace fingerprint_browser

#endif  // BRAVE_BROWSER_FINGERPRINT_BROWSER_FINGERPRINT_PROXY_SERVICE_FACTORY_H_
