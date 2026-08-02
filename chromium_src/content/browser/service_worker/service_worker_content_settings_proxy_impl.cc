/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <string>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "content/browser/storage_partition_impl.h"

#include <content/browser/service_worker/service_worker_content_settings_proxy_impl.cc>

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

void ServiceWorkerContentSettingsProxyImpl::GetBraveShieldsSettings(
    int32_t webcompat_settings_type,
    GetBraveShieldsSettingsCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  // May be shutting down.
  if (!context_wrapper_->browser_context()) {
    std::move(callback).Run(brave_shields::mojom::ShieldsSettings::New());
    return;
  }
  // Shields should also work in opaque origins.
  const GURL url = TagWorkerWebcompatType(
      origin_.GetTupleOrPrecursorTupleIfOpaque().GetURL(),
      webcompat_settings_type);
  const StoragePartitionConfig* storage_partition_config = nullptr;
  if (StoragePartitionImpl* storage_partition =
          context_wrapper_->storage_partition()) {
    storage_partition_config = &storage_partition->GetConfig();
  }
  std::move(callback).Run(
      GetContentClient()->browser()->WorkerGetBraveShieldSettings(
          url, context_wrapper_->browser_context(), storage_partition_config));
}

}  // namespace content
