/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <string>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"

#include <content/browser/worker_host/shared_worker_content_settings_proxy_impl.cc>

namespace content {

namespace {

constexpr char kWorkerWebcompatRefPrefix[] = "brave-worker-webcompat-";

GURL TagWorkerWebcompatType(const GURL& url, int32_t type) {
  GURL::Replacements replacements;
  const std::string ref =
      base::StrCat({kWorkerWebcompatRefPrefix, base::NumberToString(type)});
  replacements.SetRefStr(ref);
  return url.ReplaceComponents(replacements);
}

}  // namespace

void SharedWorkerContentSettingsProxyImpl::GetBraveShieldsSettings(
    int32_t webcompat_settings_type,
    GetBraveShieldsSettingsCallback callback) {
  // Shields should also work in opaque origins.
  const GURL url = TagWorkerWebcompatType(
      origin_.GetTupleOrPrecursorTupleIfOpaque().GetURL(),
      webcompat_settings_type);
  owner_->GetBraveShieldsSettings(url, std::move(callback));
}

}  // namespace content
