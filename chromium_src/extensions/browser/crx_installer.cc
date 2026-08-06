/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#define BRAVE_CRX_INSTALLER_ALLOW_INSTALL(extension)                           \
  do {                                                                         \
    if (off_store_install_allow_reason_ ==                                     \
            OffStoreInstallAllowedFromBraveInternalStore &&                    \
        !util::IsBraveInternalExtensionStoreUpdateUrl(                         \
            ManifestURL::GetUpdateURL(extension))) {                           \
      return CrxInstallError(                                                  \
          CrxInstallErrorType::OTHER, CrxInstallErrorDetail::MANIFEST_INVALID, \
          u"\u5185\u90e8\u63d2\u4ef6\u7684\u81ea\u52a8\u66f4\u65b0"            \
          u"\u5730\u5740\u4e0d\u53d7\u4fe1\u4efb\u3002");                      \
    }                                                                          \
  } while (false)

#include <extensions/browser/crx_installer.cc>

#undef BRAVE_CRX_INSTALLER_ALLOW_INSTALL
