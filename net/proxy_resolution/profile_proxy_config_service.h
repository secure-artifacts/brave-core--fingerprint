/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_NET_PROXY_RESOLUTION_PROFILE_PROXY_CONFIG_SERVICE_H_
#define BRAVE_NET_PROXY_RESOLUTION_PROFILE_PROXY_CONFIG_SERVICE_H_

#include "base/observer_list.h"
#include "net/base/net_export.h"
#include "net/base/proxy_server.h"
#include "net/proxy_resolution/proxy_config_service.h"
#include "net/traffic_annotation/network_traffic_annotation.h"

namespace net {

class ProxyConfigWithAnnotation;

class NET_EXPORT ProfileProxyConfigService : public ProxyConfigService {
 public:
  ProfileProxyConfigService();
  explicit ProfileProxyConfigService(ProxyServer proxy_server);
  ~ProfileProxyConfigService() override;

  void UpdateProxyServer(ProxyServer proxy_server);
  void ClearProxyServer();

  void AddObserver(Observer* observer) override;
  void RemoveObserver(Observer* observer) override;
  ConfigAvailability GetLatestProxyConfig(
      ProxyConfigWithAnnotation* config) override;

  static NetworkTrafficAnnotationTag GetProfileProxyAnnotationTagForTesting();

 private:
  void NotifyObservers();

  base::ObserverList<Observer>::Unchecked observers_;
  ProxyServer proxy_server_;
};

}  // namespace net

#endif  // BRAVE_NET_PROXY_RESOLUTION_PROFILE_PROXY_CONFIG_SERVICE_H_
