/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/net/fingerprint_browser_user_agent_network_delegate_helper.h"

#include <memory>
#include <set>
#include <string>

#include "brave/browser/fingerprint_browser/persona_service_factory.h"
#include "brave/components/brave_shields/core/browser/brave_shields_utils.h"
#include "brave/components/brave_shields/core/common/shields_settings.mojom.h"
#include "brave/components/fingerprint_browser/browser/persona.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "components/embedder_support/user_agent_utils.h"
#include "net/base/net_errors.h"
#include "net/base/url_util.h"
#include "net/http/http_request_headers.h"
#include "net/http/structured_headers.h"
#include "services/network/public/cpp/client_hints.h"
#include "services/network/public/mojom/web_client_hints_types.mojom.h"
#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"

namespace brave {

namespace {

GURL GetContentSettingURL(const GURL& url) {
  if (url.SchemeIsWSOrWSS()) {
    return net::ChangeWebSocketSchemeToHttpScheme(url);
  }
  return url;
}

template <typename T>
std::string SerializeHeaderString(const T& value) {
  return net::structured_headers::SerializeItem(
             net::structured_headers::Item(value))
      .value_or(std::string());
}

void SetClientHintHeaderIfPresent(net::HttpRequestHeaders* headers,
                                  network::mojom::WebClientHintsType type,
                                  const std::string& value,
                                  std::set<std::string>* modified_headers) {
  const auto& name = network::GetClientHintToNameMap().at(type);
  if (!headers->HasHeader(name)) {
    return;
  }
  headers->SetHeader(name, value);
  modified_headers->insert(name);
}

void ApplyUserAgentClientHintsIfPresent(
    net::HttpRequestHeaders* headers,
    blink::UserAgentMetadata metadata,
    std::set<std::string>* modified_headers) {
  using network::mojom::WebClientHintsType;

  SetClientHintHeaderIfPresent(headers, WebClientHintsType::kUA,
                               metadata.SerializeBrandMajorVersionList(),
                               modified_headers);
  SetClientHintHeaderIfPresent(headers, WebClientHintsType::kUAMobile,
                               SerializeHeaderString(metadata.mobile),
                               modified_headers);
  SetClientHintHeaderIfPresent(headers, WebClientHintsType::kUAFullVersion,
                               SerializeHeaderString(metadata.full_version),
                               modified_headers);
  SetClientHintHeaderIfPresent(headers, WebClientHintsType::kUAArch,
                               SerializeHeaderString(metadata.architecture),
                               modified_headers);
  SetClientHintHeaderIfPresent(headers, WebClientHintsType::kUAPlatform,
                               SerializeHeaderString(metadata.platform),
                               modified_headers);
  SetClientHintHeaderIfPresent(headers, WebClientHintsType::kUAPlatformVersion,
                               SerializeHeaderString(metadata.platform_version),
                               modified_headers);
  SetClientHintHeaderIfPresent(headers, WebClientHintsType::kUAModel,
                               SerializeHeaderString(metadata.model),
                               modified_headers);
  SetClientHintHeaderIfPresent(headers, WebClientHintsType::kUABitness,
                               SerializeHeaderString(metadata.bitness),
                               modified_headers);
  SetClientHintHeaderIfPresent(headers, WebClientHintsType::kUAWoW64,
                               SerializeHeaderString(metadata.wow64),
                               modified_headers);
  SetClientHintHeaderIfPresent(headers, WebClientHintsType::kUAFullVersionList,
                               metadata.SerializeBrandFullVersionList(),
                               modified_headers);
  SetClientHintHeaderIfPresent(headers, WebClientHintsType::kUAFormFactors,
                               metadata.SerializeFormFactors(),
                               modified_headers);
}

brave_shields::mojom::FarblingLevel GetUserAgentFarblingLevel(
    HostContentSettingsMap* host_content_settings_map,
    const GURL& url) {
  auto farbling_level =
      brave_shields::GetFarblingLevel(host_content_settings_map, url);
  if (farbling_level == brave_shields::mojom::FarblingLevel::OFF) {
    return farbling_level;
  }
  if (brave_shields::IsWebcompatEnabled(
          host_content_settings_map,
          ContentSettingsType::BRAVE_WEBCOMPAT_USER_AGENT, url)) {
    return brave_shields::mojom::FarblingLevel::OFF;
  }
  return farbling_level;
}

}  // namespace

template <template <typename> class T>
int OnBeforeStartTransaction_FingerprintBrowserUserAgentWork(
    net::HttpRequestHeaders* headers,
    const ResponseCallback& next_callback,
    T<BraveRequestInfo> ctx) {
  if (!ctx || !ctx->browser_context()) {
    return net::OK;
  }

  const auto* persona =
      fingerprint_browser::GetPersonaForBrowserContext(ctx->browser_context());
  if (!persona) {
    return net::OK;
  }

  const GURL content_setting_url = GetContentSettingURL(ctx->request_url());
  if (!content_setting_url.SchemeIsHTTPOrHTTPS()) {
    return net::OK;
  }

  std::string user_agent = persona->user_agent;
  blink::UserAgentMetadata metadata =
      fingerprint_browser::ToBlinkUserAgentMetadata(persona->ua_metadata);
  auto* host_content_settings_map =
      HostContentSettingsMapFactory::GetForProfile(ctx->browser_context());
  if (GetUserAgentFarblingLevel(host_content_settings_map,
                                content_setting_url) ==
      brave_shields::mojom::FarblingLevel::OFF) {
    user_agent = embedder_support::GetUserAgent();
    metadata = embedder_support::GetUserAgentMetadata();
  }

  headers->SetHeader(net::HttpRequestHeaders::kUserAgent, user_agent);
  ctx->mutable_modified_headers().insert(net::HttpRequestHeaders::kUserAgent);
  ApplyUserAgentClientHintsIfPresent(headers, metadata,
                                     &ctx->mutable_modified_headers());
  return net::OK;
}

template int OnBeforeStartTransaction_FingerprintBrowserUserAgentWork<
    std::shared_ptr>(net::HttpRequestHeaders* headers,
                     const ResponseCallback& next_callback,
                     std::shared_ptr<BraveRequestInfo> ctx);

template int OnBeforeStartTransaction_FingerprintBrowserUserAgentWork<
    base::WeakPtr>(net::HttpRequestHeaders* headers,
                   const ResponseCallback& next_callback,
                   base::WeakPtr<BraveRequestInfo> ctx);

}  // namespace brave
