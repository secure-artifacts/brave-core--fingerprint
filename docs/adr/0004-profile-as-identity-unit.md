# ADR-0004: 1 身份 = 1 Chromium Profile,可并发;代理走 per-Profile ProxyConfigService

Status: Accepted (2026-07-08)

## Context

多号运营要求身份之间**真隔离**(cookie/storage/cache/localStorage 不串)且能**并发**
(多身份同时在线)。单 Profile 原地换指纹+代理会导致存储串号,不是真多号。

调研确认:Chromium proxy 天然是 per-`Profile`/`NetworkContext`
(`ProfileNetworkContextService` 持
`ProxyConfigMonitor`);Brave 的 Tor 已有完整的 per-Profile `ProxyConfigService`
模板。

## Decision

**一个身份 = 一个 Chromium Profile**,自带独立 storage + NetworkContext + 代理 +
persona, 多个 Profile 可并发运行。

代理注入复用 Tor 的缝:`chromium_src/chrome/browser/net/proxy_config_monitor.cc`
里 `if (profile->IsTor()) ... CreateProxyConfigServiceTor`
的模式,克隆出一个通用per-Profile `ProxyConfigService`,承载 HTTP/HTTPS/SOCKS5
CONNECT 地址 + 凭证。

## Consequences

- 存储隔离、代理隔离天然成立,无需自建隔离层。
- SOCKS5 支持无认证与 RFC1929 user/pass 认证(`socks5_client_socket.cc` 的
  `SOCKS5ClientSocketAuth`)，用户名和密码必须同时为空或同时填写，各自最多 255
  UTF-8 字节；HTTP/HTTPS 代理认证走 `LoginHandler::SetAuth()`/预填
  `HttpAuthCache` 绕弹窗。
- SOCKS5 仅支持 TCP CONNECT，不支持 SOCKS4、BIND 或 UDP ASSOCIATE。
- 并发多 Profile
  = 多 NetworkContext 同时存活,内存/资源偏重,但 Chromium 原生支持。
- persona 与 proxy 都挂在 Profile 上,天然一一对应。
- v1 用户可见面仍是「当前 Profile 的 IP 设置页+开关」(用户原始最小需求);完整的Profile 管理器(批量创建/分配)是后续层,不在 v1 强制范围。
