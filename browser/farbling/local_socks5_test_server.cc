/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/farbling/local_socks5_test_server.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/stringprintf.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/thread_restrictions.h"
#include "net/base/io_buffer.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/log/net_log_source.h"
#include "net/socket/stream_socket.h"
#include "net/socket/tcp_server_socket.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"

namespace fingerprint_browser::test {

namespace {

constexpr char kProxyBody[] = "proxy";
constexpr char kFreeIpApiAustralia[] = R"({
  "ipAddress":"1.1.1.1",
  "latitude":-33.8688,
  "longitude":151.2093,
  "countryName":"Australia",
  "countryCode":"AU",
  "timeZones":["Australia/Sydney"],
  "cityName":"Sydney",
  "regionName":"New South Wales"
})";
constexpr char kIpWhoIsUnitedStates[] = R"({
  "ip":"8.8.4.4",
  "success":true,
  "country":"United States",
  "country_code":"US",
  "region":"California",
  "city":"Mountain View",
  "latitude":37.3860517,
  "longitude":-122.0838511,
  "timezone":{"id":"America/Los_Angeles"}
})";

}  // namespace

class LocalSocks5TestServer::Connection {
 public:
  Connection(LocalSocks5TestServer* owner,
             std::unique_ptr<net::StreamSocket> socket)
      : socket_(std::move(socket)), owner_(owner) {
    ReadMore();
  }

  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;
  ~Connection() = default;

 private:
  enum class Phase {
    kGreeting,
    kAuthentication,
    kConnect,
    kHttp,
    kClosed,
  };

  void ReadMore() {
    read_buffer_ = base::MakeRefCounted<net::IOBufferWithSize>(4096);
    const int result = socket_->Read(
        read_buffer_.get(), read_buffer_->size(),
        base::BindOnce(&Connection::OnRead, weak_factory_.GetWeakPtr()));
    if (result != net::ERR_IO_PENDING) {
      OnRead(result);
    }
  }

  void OnRead(int result) {
    if (result <= 0) {
      Close();
      return;
    }
    input_.append(read_buffer_->data(), result);
    ProcessInput();
  }

  void ProcessInput() {
    switch (phase_) {
      case Phase::kGreeting:
        ProcessGreeting();
        return;
      case Phase::kAuthentication:
        ProcessAuthentication();
        return;
      case Phase::kConnect:
        ProcessConnect();
        return;
      case Phase::kHttp:
        ProcessHttp();
        return;
      case Phase::kClosed:
        return;
    }
  }

  void ProcessGreeting() {
    if (input_.size() < 2) {
      ReadMore();
      return;
    }
    const size_t method_count = ByteAt(1);
    if (input_.size() < 2 + method_count || ByteAt(0) != 0x05) {
      ReadMore();
      return;
    }
    const uint8_t required_method = owner_->username_.empty() ? 0x00 : 0x02;
    bool supports_method = false;
    for (size_t i = 0; i < method_count; ++i) {
      supports_method |= ByteAt(2 + i) == required_method;
    }
    input_.erase(0, 2 + method_count);
    if (!supports_method) {
      Write(std::string({0x05, static_cast<char>(0xff)}), Phase::kClosed);
      return;
    }
    Write(std::string({0x05, static_cast<char>(required_method)}),
          required_method == 0x02 ? Phase::kAuthentication : Phase::kConnect);
  }

  void ProcessAuthentication() {
    if (input_.size() < 2) {
      ReadMore();
      return;
    }
    const size_t username_length = ByteAt(1);
    if (input_.size() < 3 + username_length) {
      ReadMore();
      return;
    }
    const size_t password_length = ByteAt(2 + username_length);
    const size_t request_length = 3 + username_length + password_length;
    if (input_.size() < request_length) {
      ReadMore();
      return;
    }
    const std::string username = input_.substr(2, username_length);
    const std::string password =
        input_.substr(3 + username_length, password_length);
    const bool accepted = ByteAt(0) == 0x01 && username == owner_->username_ &&
                          password == owner_->password_;
    input_.erase(0, request_length);
    owner_->RecordAuthentication(accepted);
    Write(std::string({0x01, static_cast<char>(accepted ? 0x00 : 0x01)}),
          accepted ? Phase::kConnect : Phase::kClosed);
  }

  void ProcessConnect() {
    if (input_.size() < 5) {
      ReadMore();
      return;
    }
    if (ByteAt(0) != 0x05 || ByteAt(1) != 0x01 || ByteAt(3) != 0x03) {
      Close();
      return;
    }
    const size_t host_length = ByteAt(4);
    const size_t request_length = 7 + host_length;
    if (input_.size() < request_length) {
      ReadMore();
      return;
    }
    owner_->RecordTargetHost(input_.substr(5, host_length));
    input_.erase(0, request_length);
    Write(std::string("\x05\x00\x00\x01\x7f\x00\x00\x01\x00\x00", 10),
          Phase::kHttp);
  }

  void ProcessHttp() {
    const size_t headers_end = input_.find("\r\n\r\n");
    if (headers_end == std::string::npos) {
      ReadMore();
      return;
    }
    std::string body = kProxyBody;
    std::string content_type = "text/plain";
    if (input_.find("/geo-primary") != std::string::npos) {
      body = kFreeIpApiAustralia;
      content_type = "application/json";
    } else if (input_.find("/geo-fallback") != std::string::npos) {
      body = kIpWhoIsUnitedStates;
      content_type = "application/json";
    }
    Write(base::StringPrintf(
              "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
              "Connection: close\r\n\r\n%s",
              content_type.c_str(), body.size(), body.c_str()),
          Phase::kClosed);
  }

  void Write(std::string data, Phase next_phase) {
    phase_after_write_ = next_phase;
    const size_t size = data.size();
    write_buffer_ = base::MakeRefCounted<net::DrainableIOBuffer>(
        base::MakeRefCounted<net::StringIOBuffer>(std::move(data)), size);
    WriteMore();
  }

  void WriteMore() {
    const int result = socket_->Write(
        write_buffer_.get(), write_buffer_->BytesRemaining(),
        base::BindOnce(&Connection::OnWrite, weak_factory_.GetWeakPtr()),
        TRAFFIC_ANNOTATION_FOR_TESTS);
    if (result != net::ERR_IO_PENDING) {
      OnWrite(result);
    }
  }

  void OnWrite(int result) {
    if (result <= 0) {
      Close();
      return;
    }
    write_buffer_->DidConsume(result);
    if (write_buffer_->BytesRemaining() > 0) {
      WriteMore();
      return;
    }
    phase_ = phase_after_write_;
    if (phase_ == Phase::kClosed) {
      Close();
      return;
    }
    ProcessInput();
  }

  uint8_t ByteAt(size_t index) const {
    return static_cast<uint8_t>(input_[index]);
  }

  void Close() {
    phase_ = Phase::kClosed;
    socket_.reset();
  }

  std::unique_ptr<net::StreamSocket> socket_;
  scoped_refptr<net::IOBufferWithSize> read_buffer_;
  scoped_refptr<net::DrainableIOBuffer> write_buffer_;
  std::string input_;
  Phase phase_ = Phase::kGreeting;
  Phase phase_after_write_ = Phase::kClosed;
  raw_ptr<LocalSocks5TestServer> owner_;
  base::WeakPtrFactory<Connection> weak_factory_{this};
};

class LocalSocks5TestServer::ServerState {
 public:
  explicit ServerState(LocalSocks5TestServer* owner)
      : socket_(nullptr, net::NetLogSource()), owner_(owner) {}

  int Start() {
    if (socket_.Listen(net::IPEndPoint(net::IPAddress::IPv4Localhost(), 0), 8,
                       /*ipv6_only=*/std::nullopt) != net::OK) {
      return 0;
    }
    net::IPEndPoint address;
    if (socket_.GetLocalAddress(&address) != net::OK) {
      return 0;
    }
    Accept();
    return address.port();
  }

 private:
  void Accept() {
    const int result = socket_.Accept(
        &accepted_socket_,
        base::BindOnce(&ServerState::OnAccepted, weak_factory_.GetWeakPtr()));
    if (result != net::ERR_IO_PENDING) {
      OnAccepted(result);
    }
  }

  void OnAccepted(int result) {
    if (result != net::OK) {
      return;
    }
    connections_.push_back(std::make_unique<Connection>(
        owner_.get(), std::move(accepted_socket_)));
    Accept();
  }

  net::TCPServerSocket socket_;
  std::unique_ptr<net::StreamSocket> accepted_socket_;
  std::vector<std::unique_ptr<Connection>> connections_;
  raw_ptr<LocalSocks5TestServer> owner_;
  base::WeakPtrFactory<ServerState> weak_factory_{this};
};

LocalSocks5TestServer::LocalSocks5TestServer(std::string username,
                                             std::string password)
    : username_(std::move(username)),
      password_(std::move(password)),
      thread_("SOCKS5 test server") {}

LocalSocks5TestServer::~LocalSocks5TestServer() {
  Stop();
}

bool LocalSocks5TestServer::Start() {
  base::Thread::Options options;
  options.message_pump_type = base::MessagePumpType::IO;
  if (!thread_.StartWithOptions(std::move(options)) ||
      !thread_.WaitUntilThreadStarted()) {
    return false;
  }

  base::WaitableEvent started;
  thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&LocalSocks5TestServer::StartOnServerThread,
                                base::Unretained(this), &started));
  base::ScopedAllowBaseSyncPrimitivesForTesting allow_wait;
  started.Wait();
  return port_ > 0;
}

void LocalSocks5TestServer::Stop() {
  if (!thread_.IsRunning()) {
    return;
  }
  base::WaitableEvent stopped;
  thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&LocalSocks5TestServer::StopOnServerThread,
                                base::Unretained(this), &stopped));
  {
    base::ScopedAllowBaseSyncPrimitivesForTesting allow_wait;
    stopped.Wait();
    thread_.Stop();
  }
}

bool LocalSocks5TestServer::SawTargetHost(std::string_view host) const {
  base::AutoLock lock(lock_);
  return std::ranges::find(target_hosts_, host) != target_hosts_.end();
}

int LocalSocks5TestServer::successful_authentications() const {
  base::AutoLock lock(lock_);
  return successful_authentications_;
}

int LocalSocks5TestServer::rejected_authentications() const {
  base::AutoLock lock(lock_);
  return rejected_authentications_;
}

void LocalSocks5TestServer::StartOnServerThread(base::WaitableEvent* started) {
  state_ = std::make_unique<ServerState>(this);
  port_ = state_->Start();
  started->Signal();
}

void LocalSocks5TestServer::StopOnServerThread(base::WaitableEvent* stopped) {
  state_.reset();
  stopped->Signal();
}

void LocalSocks5TestServer::RecordTargetHost(std::string host) {
  base::AutoLock lock(lock_);
  target_hosts_.push_back(std::move(host));
}

void LocalSocks5TestServer::RecordAuthentication(bool accepted) {
  base::AutoLock lock(lock_);
  if (accepted) {
    ++successful_authentications_;
  } else {
    ++rejected_authentications_;
  }
}

}  // namespace fingerprint_browser::test
