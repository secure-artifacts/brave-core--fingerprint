/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/net/proxy_resolution/profile_proxy_config_service.h"

#include "net/base/host_port_pair.h"
#include "net/base/proxy_chain.h"
#include "net/base/proxy_server.h"
#include "net/proxy_resolution/proxy_config.h"
#include "net/proxy_resolution/proxy_config_with_annotation.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace {

ProxyServer SingleProxyServer(const ProxyConfigWithAnnotation& config) {
  const ProxyList& single_proxies = config.value().proxy_rules().single_proxies;
  EXPECT_FALSE(single_proxies.IsEmpty());
  const ProxyChain& chain = single_proxies.First();
  EXPECT_TRUE(chain.is_single_proxy());
  return chain.GetProxyServer(/*chain_index=*/0);
}

ProxyServer AuthProxyServer(ProxyServer::Scheme scheme,
                            std::string_view username,
                            std::string_view password,
                            std::string_view host,
                            uint16_t port) {
  return ProxyServer(scheme, HostPortPair(username, password, host, port));
}

class RecordingObserver : public ProxyConfigService::Observer {
 public:
  void OnProxyConfigChanged(
      const ProxyConfigWithAnnotation& config,
      ProxyConfigService::ConfigAvailability availability) override {
    ++change_count_;
    config_ = config;
    availability_ = availability;
  }

  int change_count() const { return change_count_; }
  ProxyConfigService::ConfigAvailability availability() const {
    return availability_;
  }
  const ProxyConfigWithAnnotation& config() const { return config_; }

 private:
  int change_count_ = 0;
  ProxyConfigService::ConfigAvailability availability_ =
      ProxyConfigService::CONFIG_PENDING;
  ProxyConfigWithAnnotation config_;
};

}  // namespace

TEST(ProfileProxyConfigServiceTest, EmptyConfigIsUnset) {
  ProfileProxyConfigService service;
  ProxyConfigWithAnnotation config;

  EXPECT_EQ(ProxyConfigService::CONFIG_UNSET,
            service.GetLatestProxyConfig(&config));
}

TEST(ProfileProxyConfigServiceTest, ReturnsHttpProxyWithCredentials) {
  ProfileProxyConfigService service(AuthProxyServer(
      ProxyServer::SCHEME_HTTP, "user", "pass", "proxy.example", 8080));
  ProxyConfigWithAnnotation config;

  ASSERT_EQ(ProxyConfigService::CONFIG_VALID,
            service.GetLatestProxyConfig(&config));
  EXPECT_EQ(ProxyConfig::ProxyRules::Type::PROXY_LIST,
            config.value().proxy_rules().type);
  const ProxyServer proxy_server = SingleProxyServer(config);
  EXPECT_EQ(ProxyServer::SCHEME_HTTP, proxy_server.scheme());
  EXPECT_EQ("proxy.example", proxy_server.host_port_pair().host());
  EXPECT_EQ(8080, proxy_server.host_port_pair().port());
  EXPECT_EQ("user", proxy_server.host_port_pair().username());
  EXPECT_EQ("pass", proxy_server.host_port_pair().password());

  const auto tag =
      ProfileProxyConfigService::GetProfileProxyAnnotationTagForTesting();
  EXPECT_EQ(tag.unique_id_hash_code,
            config.traffic_annotation().unique_id_hash_code);
}

TEST(ProfileProxyConfigServiceTest, ReturnsSocks5ProxyWithCredentials) {
  ProfileProxyConfigService service(AuthProxyServer(ProxyServer::SCHEME_SOCKS5,
                                                    "socks-user", "socks-pass",
                                                    "127.0.0.1", 1080));
  ProxyConfigWithAnnotation config;

  ASSERT_EQ(ProxyConfigService::CONFIG_VALID,
            service.GetLatestProxyConfig(&config));
  const ProxyServer proxy_server = SingleProxyServer(config);
  EXPECT_EQ(ProxyServer::SCHEME_SOCKS5, proxy_server.scheme());
  EXPECT_EQ("127.0.0.1", proxy_server.host_port_pair().host());
  EXPECT_EQ(1080, proxy_server.host_port_pair().port());
  EXPECT_EQ("socks-user", proxy_server.host_port_pair().username());
  EXPECT_EQ("socks-pass", proxy_server.host_port_pair().password());
}

TEST(ProfileProxyConfigServiceTest, UpdateNotifiesObservers) {
  ProfileProxyConfigService service;
  RecordingObserver observer;
  service.AddObserver(&observer);

  service.UpdateProxyServer(AuthProxyServer(ProxyServer::SCHEME_HTTP, "user",
                                            "pass", "proxy.example", 8080));

  EXPECT_EQ(1, observer.change_count());
  EXPECT_EQ(ProxyConfigService::CONFIG_VALID, observer.availability());
  const ProxyServer proxy_server = SingleProxyServer(observer.config());
  EXPECT_EQ(ProxyServer::SCHEME_HTTP, proxy_server.scheme());
  EXPECT_EQ("proxy.example", proxy_server.host_port_pair().host());
  EXPECT_EQ("user", proxy_server.host_port_pair().username());
  EXPECT_EQ("pass", proxy_server.host_port_pair().password());

  service.ClearProxyServer();

  EXPECT_EQ(2, observer.change_count());
  EXPECT_EQ(ProxyConfigService::CONFIG_UNSET, observer.availability());

  service.RemoveObserver(&observer);
  service.UpdateProxyServer(ProxyServer::FromSchemeHostAndPort(
      ProxyServer::SCHEME_HTTP, "proxy2.example", "8081"));

  EXPECT_EQ(2, observer.change_count());
}

}  // namespace net
