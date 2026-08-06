/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "chrome/browser/profiles/profile.h"
#include "extensions/browser/extension_util.h"

namespace download_crx_util {

bool IsBraveInternalExtensionStoreInstallAllowed(
    Profile* profile,
    const download::DownloadItem& item) {
  return profile && profile->IsRegularProfile() && !profile->IsTor() &&
         extensions::util::IsBraveInternalExtensionStoreDownload(item);
}

}  // namespace download_crx_util

#define BRAVE_DOWNLOAD_CRX_UTIL_CONFIGURE_INSTALLER(profile, item, installer) \
  do {                                                                        \
    if (download_crx_util::IsBraveInternalExtensionStoreInstallAllowed(       \
            profile, item)) {                                                 \
      installer->set_off_store_install_allow_reason(                          \
          extensions::CrxInstaller::                                          \
              OffStoreInstallAllowedFromBraveInternalStore);                  \
    }                                                                         \
  } while (false)

#define BRAVE_DOWNLOAD_CRX_UTIL_IS_TRUSTED_EXTENSION_DOWNLOAD(profile, item) \
  download_crx_util::IsBraveInternalExtensionStoreInstallAllowed(profile,    \
                                                                 item) ||

#define BRAVE_DOWNLOAD_CRX_UTIL_IS_EXTENSION_DOWNLOAD_ALLOWED(profile, item) \
  (!extensions::util::IsBraveInternalExtensionStoreDownload(item) ||         \
   download_crx_util::IsBraveInternalExtensionStoreInstallAllowed(profile,   \
                                                                  item))

#include <chrome/browser/download/download_crx_util.cc>

#undef BRAVE_DOWNLOAD_CRX_UTIL_CONFIGURE_INSTALLER
#undef BRAVE_DOWNLOAD_CRX_UTIL_IS_EXTENSION_DOWNLOAD_ALLOWED
#undef BRAVE_DOWNLOAD_CRX_UTIL_IS_TRUSTED_EXTENSION_DOWNLOAD
