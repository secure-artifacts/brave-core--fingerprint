/* Copyright (c) 2025 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>

#include "base/no_destructor.h"
#include "base/strings/escape.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "components/download/public/common/download_item.h"
#include "extensions/common/extension.h"
#include "net/http/http_content_disposition.h"
#include "url/origin.h"
#include "url/url_constants.h"

#define IsExtensionDownload IsExtensionDownload_ChromiumImpl
#include <extensions/browser/extension_util.cc>
#undef IsExtensionDownload

namespace extensions::util {

namespace {

constexpr char kInternalExtensionStoreOrigin[] =
    "https://plugin.afferdmail.com";
constexpr char kInternalExtensionStorePath[] = "/crx/";
constexpr char kCrxSuffix[] = ".crx";
constexpr char kUpdateManifestSuffix[] = "/update.xml";
constexpr char kGenericBinaryMimeType[] = "application/octet-stream";

GURL& InternalExtensionStoreOriginOverride() {
  static base::NoDestructor<GURL> origin;
  return *origin;
}

GURL GetInternalExtensionStoreOrigin() {
  const GURL& override = InternalExtensionStoreOriginOverride();
  return override.is_valid() ? override : GURL(kInternalExtensionStoreOrigin);
}

bool HasInternalExtensionStoreOrigin(const GURL& url) {
  return url.is_valid() && !url.has_username() && !url.has_password() &&
         url.SchemeIs(url::kHttpsScheme) &&
         url::Origin::Create(url).IsSameOriginWith(
             GetInternalExtensionStoreOrigin());
}

bool HasSafeInternalPath(const GURL& url) {
  std::string unescaped_path;
  if (!base::UnescapeBinaryURLComponentSafe(url.path_piece(),
                                            /*fail_on_path_separators=*/true,
                                            &unescaped_path)) {
    return false;
  }
  for (std::string_view segment : base::SplitStringPiece(
           unescaped_path, "/", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL)) {
    if (segment == "." || segment == "..") {
      return false;
    }
  }
  return true;
}

bool IsInternalCrxPath(const GURL& url) {
  return HasSafeInternalPath(url) &&
         base::StartsWith(url.path_piece(), kInternalExtensionStorePath,
                          base::CompareCase::SENSITIVE) &&
         base::EndsWith(url.path_piece(), kCrxSuffix,
                        base::CompareCase::INSENSITIVE_ASCII);
}

bool HasSupportedInternalCrxMimeType(
    const download::DownloadItem& download_item) {
  const auto is_supported = [](std::string_view mime_type) {
    return mime_type == Extension::kMimeType ||
           mime_type == kGenericBinaryMimeType;
  };
  return is_supported(download_item.GetMimeType()) ||
         is_supported(download_item.GetOriginalMimeType());
}

bool IsCrxFilename(std::string_view filename) {
  return base::EndsWith(filename, kCrxSuffix,
                        base::CompareCase::INSENSITIVE_ASCII);
}

bool HasSupportedInternalCrxDisposition(
    const download::DownloadItem& download_item) {
  const std::string suggested_filename = download_item.GetSuggestedFilename();
  if (!suggested_filename.empty() && !IsCrxFilename(suggested_filename)) {
    return false;
  }

  net::HttpContentDisposition disposition(download_item.GetContentDisposition(),
                                          std::string());
  return disposition.filename().empty() ||
         IsCrxFilename(disposition.filename());
}

}  // namespace

bool IsBraveInternalExtensionStoreDownload(
    const download::DownloadItem& download_item) {
  if (!download_item.HasUserGesture() ||
      download_item.GetTargetDisposition() ==
          download::DownloadItem::TARGET_DISPOSITION_PROMPT) {
    return false;
  }

  const GURL& final_url = download_item.GetURL();
  const GURL& original_url = download_item.GetOriginalUrl();
  const GURL& referrer_url = download_item.GetReferrerUrl();
  const auto& url_chain = download_item.GetUrlChain();
  return HasInternalExtensionStoreOrigin(final_url) &&
         HasInternalExtensionStoreOrigin(original_url) &&
         HasInternalExtensionStoreOrigin(referrer_url) &&
         IsInternalCrxPath(final_url) && IsInternalCrxPath(original_url) &&
         !url_chain.empty() &&
         std::ranges::all_of(url_chain,
                             [](const GURL& url) {
                               return HasInternalExtensionStoreOrigin(url) &&
                                      IsInternalCrxPath(url);
                             }) &&
         base::StartsWith(referrer_url.path_piece(),
                          kInternalExtensionStorePath,
                          base::CompareCase::SENSITIVE) &&
         HasSupportedInternalCrxMimeType(download_item) &&
         HasSupportedInternalCrxDisposition(download_item);
}

bool IsBraveInternalExtensionStoreUpdateUrl(const GURL& update_url) {
  if (!HasInternalExtensionStoreOrigin(update_url) ||
      !HasSafeInternalPath(update_url) ||
      !base::StartsWith(update_url.path_piece(), kInternalExtensionStorePath,
                        base::CompareCase::SENSITIVE) ||
      !base::EndsWith(update_url.path_piece(), kUpdateManifestSuffix,
                      base::CompareCase::SENSITIVE)) {
    return false;
  }

  return update_url.path_piece().size() >
         std::string_view(kInternalExtensionStorePath).size() +
             std::string_view(kUpdateManifestSuffix).size();
}

std::unique_ptr<base::AutoReset<GURL>>
OverrideBraveInternalExtensionStoreOriginForTesting(const GURL& origin) {
  return std::make_unique<base::AutoReset<GURL>>(
      &InternalExtensionStoreOriginOverride(), origin);
}

bool IsExtensionDownload(const download::DownloadItem& download_item) {
  return IsExtensionDownload_ChromiumImpl(download_item) ||
         IsBraveInternalExtensionStoreDownload(download_item);
}

}  // namespace extensions::util
