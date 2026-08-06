/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#define BRAVE_CHROME_EXTENSION_SYSTEM_CONFIGURE_UPDATE_INSTALLER(      \
    profile, extension_id, installer)                                  \
  do {                                                                 \
    Profile* original_profile = profile->GetOriginalProfile();         \
    const Extension* installed_extension =                             \
        ExtensionRegistry::Get(original_profile)                       \
            ->GetInstalledExtension(extension_id);                     \
    if (installed_extension &&                                         \
        util::IsBraveInternalExtensionStoreUpdateUrl(                  \
            ManifestURL::GetUpdateURL(installed_extension))) {         \
      installer->set_off_store_install_allow_reason(                   \
          CrxInstaller::OffStoreInstallAllowedFromBraveInternalStore); \
    }                                                                  \
  } while (false)

#include <chrome/browser/extensions/chrome_extension_system.cc>

#undef BRAVE_CHROME_EXTENSION_SYSTEM_CONFIGURE_UPDATE_INSTALLER
