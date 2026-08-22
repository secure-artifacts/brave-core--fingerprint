/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_CHROMIUM_SRC_NET_SOCKET_SOCKS5_CLIENT_SOCKET_UNITTEST_H_
#define BRAVE_CHROMIUM_SRC_NET_SOCKET_SOCKS5_CLIENT_SOCKET_UNITTEST_H_

TEST_F(SOCKS5ClientSocketTest,
       CompleteHandshakeWithoutAuthenticationThroughAuthSocket) {
  const char kOkRequest[] = {
      0x05, 0x01, 0x00, 0x03, 0x09, 'l', 'o',  'c',
      'a',  'l',  'h',  'o',  's',  't', 0x00, 0x50,
  };
  MockWrite data_writes[] = {
      MockWrite(ASYNC, kSOCKS5GreetRequest),
      MockWrite(ASYNC, base::as_byte_span(kOkRequest)),
  };
  MockRead data_reads[] = {
      MockRead(ASYNC, kSOCKS5GreetResponse),
      MockRead(ASYNC, kSOCKS5OkResponse),
  };

  user_sock_ = BuildMockAuthSocket(data_reads, data_writes, "localhost", 80, "",
                                   "", NetLog::Get());

  int rv = user_sock_->Connect(callback_.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback_.WaitForResult(), IsOk());
  EXPECT_TRUE(user_sock_->IsConnected());
}

TEST_F(SOCKS5ClientSocketTest, UsernamePasswordAuthUnexpectedlyClosed) {
  const char kSOCKS5AuthGreetRequest[] = {
      0x05,  // Version.
      0x01,  // One authentication method.
      0x02,  // Username/password authentication.
  };
  const char kSOCKS5AuthGreetResponse[] = {
      0x05,  // Version.
      0x02,  // Username/password authentication.
  };
  const char kSOCKS5AuthRequest[] = {
      0x01,  // RFC1929 version.
      0x04,  // Username length.
      'u',  's', 'e', 'r',
      0x04,  // Password length.
      'p',  'a', 's', 's',
  };

  MockWrite data_writes[] = {
      MockWrite(ASYNC, base::as_byte_span(kSOCKS5AuthGreetRequest)),
      MockWrite(ASYNC, base::as_byte_span(kSOCKS5AuthRequest)),
  };
  MockRead data_reads[] = {
      MockRead(ASYNC, base::as_byte_span(kSOCKS5AuthGreetResponse)),
      MockRead(SYNCHRONOUS, 0),
  };

  user_sock_ = BuildMockAuthSocket(data_reads, data_writes, "localhost", 80,
                                   "user", "pass", NetLog::Get());

  int rv = user_sock_->Connect(callback_.callback());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  rv = callback_.WaitForResult();

  EXPECT_THAT(rv, IsError(ERR_SOCKS_CONNECTION_FAILED));
  EXPECT_FALSE(user_sock_->IsConnected());
}

TEST_F(SOCKS5ClientSocketTest,
       UsernamePasswordAuthRejectsIncompleteCredentials) {
  const char kSOCKS5AuthGreetRequest[] = {
      0x05,
      0x01,
      0x02,
  };
  const char kSOCKS5AuthGreetResponse[] = {
      0x05,
      0x02,
  };
  const auto expect_rejected = [&](std::string username, std::string password) {
    MockWrite data_writes[] = {
        MockWrite(ASYNC, base::as_byte_span(kSOCKS5AuthGreetRequest)),
    };
    MockRead data_reads[] = {
        MockRead(ASYNC, base::as_byte_span(kSOCKS5AuthGreetResponse)),
    };
    user_sock_ = BuildMockAuthSocket(data_reads, data_writes, "localhost", 80,
                                     std::move(username), std::move(password),
                                     NetLog::Get());
    TestCompletionCallback callback;
    int rv = user_sock_->Connect(callback.callback());
    EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
    EXPECT_THAT(callback.WaitForResult(), IsError(ERR_INVALID_ARGUMENT));
    EXPECT_FALSE(user_sock_->IsConnected());
  };

  expect_rejected("user", "");
  expect_rejected("", "pass");
}

TEST_F(SOCKS5ClientSocketTest,
       UsernamePasswordAuthRejectsOversizedCredentials) {
  const char kSOCKS5AuthGreetRequest[] = {
      0x05,
      0x01,
      0x02,
  };
  const char kSOCKS5AuthGreetResponse[] = {
      0x05,
      0x02,
  };
  const auto expect_rejected = [&](std::string username, std::string password) {
    MockWrite data_writes[] = {
        MockWrite(ASYNC, base::as_byte_span(kSOCKS5AuthGreetRequest)),
    };
    MockRead data_reads[] = {
        MockRead(ASYNC, base::as_byte_span(kSOCKS5AuthGreetResponse)),
    };
    user_sock_ = BuildMockAuthSocket(data_reads, data_writes, "localhost", 80,
                                     std::move(username), std::move(password),
                                     NetLog::Get());
    TestCompletionCallback callback;
    int rv = user_sock_->Connect(callback.callback());
    EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
    EXPECT_THAT(callback.WaitForResult(), IsError(ERR_INVALID_ARGUMENT));
    EXPECT_FALSE(user_sock_->IsConnected());
  };

  expect_rejected(std::string(256, 'u'), "pass");
  expect_rejected("user", std::string(256, 'p'));
}

#endif  // BRAVE_CHROMIUM_SRC_NET_SOCKET_SOCKS5_CLIENT_SOCKET_UNITTEST_H_
