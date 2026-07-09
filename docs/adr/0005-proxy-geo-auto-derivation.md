# ADR-0005: 代理 geo 按 IP 自动推导 + WebRTC 防漏

Status: Accepted (2026-07-08)

## Context

Chromium 不会根据代理 IP 自动设时区/geo/语言——它们读宿主 OS。所以加代理但不改这些 =
IP 在美国、timezone 却是 Asia/Shanghai → 最常见的秒封矛盾。且 WebRTC 的 STUN/ICE 走 UDP
绕过代理直连,泄漏真实公网 IP → 一次泄漏永久关联所有「独立」Profile。

用户侧只想填 IP+账号密码,不想手填时区经纬度。

## Decision

**输入代理 IP 后,用 IP 地理库(MaxMind GeoIP2 或等价)自动查出国家/时区/经纬度,
自动配置该 Profile 的:**
- 时区(Intl / Date)
- geolocation(经纬度)
- Accept-Language / navigator.languages
- (可选)高级用户手动覆盖

**同时强制 WebRTC 防漏:** `--force-webrtc-ip-handling-policy=disable_non_proxied_udp`
(唯一能彻底防公网 IP 泄漏的策略)。

## Consequences

- 用户体验好:只填 IP+凭证,L3 一致性自动搞定。
- 需集成一个 IP 地理数据库(离线库或 API),并在 Profile 创建/改代理时触发推导。
- WebRTC 防漏代价:非代理 UDP 被禁 → 真 WebRTC 音视频通话会断。多号场景可接受。
- 这四项(时区/geo/语言/WebRTC)必须与代理**同步更新**,任一漏配 = 制造矛盾,比不加代理更糟。
- 时区/geo override 的 per-profile 落地方式待定(ICU 级 patch vs CDP Emulation),设计阶段再定。
