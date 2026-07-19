/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_CHROMIUM_SRC_NET_SOCKET_SOCKS5_CLIENT_SOCKET_UNITTEST_H_
#define BRAVE_CHROMIUM_SRC_NET_SOCKET_SOCKS5_CLIENT_SOCKET_UNITTEST_H_

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

#endif  // BRAVE_CHROMIUM_SRC_NET_SOCKET_SOCKS5_CLIENT_SOCKET_UNITTEST_H_
