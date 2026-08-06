/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "chrome/browser/download/download_crx_util.h"

#define BRAVE_CHROME_DOWNLOAD_MANAGER_DELEGATE_APPLY_PREFS_INSTALL_REASON( \
    profile, item)                                                         \
  !download_crx_util::IsBraveInternalExtensionStoreInstallAllowed(profile, item)

#define BRAVE_CHROME_DOWNLOAD_MANAGER_DELEGATE_REJECT_EXTENSION_DOWNLOAD(   \
    profile, item)                                                          \
  (extensions::util::IsBraveInternalExtensionStoreDownload(item) &&         \
   !download_crx_util::IsBraveInternalExtensionStoreInstallAllowed(profile, \
                                                                   item))

#include <chrome/browser/download/chrome_download_manager_delegate.cc>

#undef BRAVE_CHROME_DOWNLOAD_MANAGER_DELEGATE_APPLY_PREFS_INSTALL_REASON
#undef BRAVE_CHROME_DOWNLOAD_MANAGER_DELEGATE_REJECT_EXTENSION_DOWNLOAD
