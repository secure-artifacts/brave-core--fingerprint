/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_CHROMIUM_SRC_CHROME_BROWSER_DOWNLOAD_DOWNLOAD_CRX_UTIL_H_
#define BRAVE_CHROMIUM_SRC_CHROME_BROWSER_DOWNLOAD_DOWNLOAD_CRX_UTIL_H_

#include <chrome/browser/download/download_crx_util.h>  // IWYU pragma: export

namespace download_crx_util {

bool IsBraveInternalExtensionStoreInstallAllowed(
    Profile* profile,
    const download::DownloadItem& item);

}  // namespace download_crx_util

#endif  // BRAVE_CHROMIUM_SRC_CHROME_BROWSER_DOWNLOAD_DOWNLOAD_CRX_UTIL_H_
