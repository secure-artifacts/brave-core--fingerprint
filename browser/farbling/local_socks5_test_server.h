/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_FARBLING_LOCAL_SOCKS5_TEST_SERVER_H_
#define BRAVE_BROWSER_FARBLING_LOCAL_SOCKS5_TEST_SERVER_H_

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/synchronization/lock.h"
#include "base/threading/thread.h"

namespace base {
class WaitableEvent;
}

namespace fingerprint_browser::test {

class LocalSocks5TestServer {
 public:
  LocalSocks5TestServer(std::string username, std::string password);
  LocalSocks5TestServer(const LocalSocks5TestServer&) = delete;
  LocalSocks5TestServer& operator=(const LocalSocks5TestServer&) = delete;
  ~LocalSocks5TestServer();

  bool Start();
  void Stop();

  int port() const { return port_; }
  bool SawTargetHost(std::string_view host) const;
  int successful_authentications() const;
  int rejected_authentications() const;

 private:
  class Connection;
  class ServerState;

  void StartOnServerThread(base::WaitableEvent* started);
  void StopOnServerThread(base::WaitableEvent* stopped);
  void RecordTargetHost(std::string host);
  void RecordAuthentication(bool accepted);

  const std::string username_;
  const std::string password_;
  base::Thread thread_;
  std::unique_ptr<ServerState> state_;
  int port_ = 0;
  mutable base::Lock lock_;
  std::vector<std::string> target_hosts_ GUARDED_BY(lock_);
  int successful_authentications_ GUARDED_BY(lock_) = 0;
  int rejected_authentications_ GUARDED_BY(lock_) = 0;
};

}  // namespace fingerprint_browser::test

#endif  // BRAVE_BROWSER_FARBLING_LOCAL_SOCKS5_TEST_SERVER_H_
