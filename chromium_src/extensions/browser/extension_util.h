/* Copyright (c) 2025 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_CHROMIUM_SRC_EXTENSIONS_BROWSER_EXTENSION_UTIL_H_
#define BRAVE_CHROMIUM_SRC_EXTENSIONS_BROWSER_EXTENSION_UTIL_H_

#include <memory>

#include "base/auto_reset.h"

// This override is used to add convenience functions to avoid adding build
// dependencies.

#include <extensions/browser/extension_util.h>  // IWYU pragma: export

#include "extensions/common/constants.h"

namespace download {
class DownloadItem;
}

namespace extensions::util {

bool IsBraveInternalExtensionStoreDownload(
    const download::DownloadItem& download_item);
bool IsBraveInternalExtensionStoreUpdateUrl(const GURL& update_url);

std::unique_ptr<base::AutoReset<GURL>>
OverrideBraveInternalExtensionStoreOriginForTesting(const GURL& origin);

}  // namespace extensions::util

#endif  // BRAVE_CHROMIUM_SRC_EXTENSIONS_BROWSER_EXTENSION_UTIL_H_
