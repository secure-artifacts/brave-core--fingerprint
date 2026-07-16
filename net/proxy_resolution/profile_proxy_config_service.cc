/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/net/proxy_resolution/profile_proxy_config_service.h"

#include <utility>

#include "net/proxy_resolution/proxy_config.h"
#include "net/proxy_resolution/proxy_config_with_annotation.h"

namespace net {
namespace {

constexpr NetworkTrafficAnnotationTag kProfileProxyTrafficAnnotation =
    DefineNetworkTrafficAnnotation("profile_proxy_config", R"(
      semantics {
        sender: "Profile Proxy Config"
        description:
          "Provides proxy configuration for a single browser profile."
        trigger:
          "Whenever a network request is made from a profile that has this "
          "proxy enabled."
        data:
          "Proxy server configuration."
        destination: OTHER
        destination_other:
          "The proxy server specified in the profile configuration."
      }
      policy {
        cookies_allowed: NO
        setting:
          "Users can configure or disable this proxy in the profile settings."
        policy_exception_justification: "Not implemented."
      })");

ProxyConfigWithAnnotation BuildProfileProxyConfig(
    const ProxyServer& proxy_server) {
  ProxyConfig proxy_config;
  proxy_config.proxy_rules().type = ProxyConfig::ProxyRules::Type::PROXY_LIST;
  proxy_config.proxy_rules().single_proxies.SetSingleProxyServer(proxy_server);
  proxy_config.proxy_rules().bypass_rules.AddRulesToSubtractImplicit();

  return ProxyConfigWithAnnotation(proxy_config,
                                   kProfileProxyTrafficAnnotation);
}

}  // namespace

ProfileProxyConfigService::ProfileProxyConfigService() = default;

ProfileProxyConfigService::ProfileProxyConfigService(ProxyServer proxy_server)
    : proxy_server_(std::move(proxy_server)) {}

ProfileProxyConfigService::~ProfileProxyConfigService() = default;

void ProfileProxyConfigService::UpdateProxyServer(ProxyServer proxy_server) {
  proxy_server_ = std::move(proxy_server);
  NotifyObservers();
}

void ProfileProxyConfigService::ClearProxyServer() {
  proxy_server_ = ProxyServer();
  NotifyObservers();
}

void ProfileProxyConfigService::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void ProfileProxyConfigService::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

ProxyConfigService::ConfigAvailability
ProfileProxyConfigService::GetLatestProxyConfig(
    ProxyConfigWithAnnotation* config) {
  if (!proxy_server_.is_valid()) {
    return CONFIG_UNSET;
  }

  *config = BuildProfileProxyConfig(proxy_server_);
  return CONFIG_VALID;
}

NetworkTrafficAnnotationTag
ProfileProxyConfigService::GetProfileProxyAnnotationTagForTesting() {
  return kProfileProxyTrafficAnnotation;
}

void ProfileProxyConfigService::NotifyObservers() {
  ProxyConfigWithAnnotation config;
  const ConfigAvailability availability = GetLatestProxyConfig(&config);

  for (auto& observer : observers_) {
    observer.OnProxyConfigChanged(config, availability);
  }
}

}  // namespace net
