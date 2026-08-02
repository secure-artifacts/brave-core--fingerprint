# ADR-0005: 代理 geo 按 IP 自动推导 + WebRTC 防漏

Status: Accepted (2026-07-08)

## Context

Chromium 不会根据代理 IP 自动设时区/geo/语言——它们读宿主 OS。所以加代理但不改这些 =
IP 在美国、timezone 却是 Asia/Shanghai
→ 最常见的秒封矛盾。且 WebRTC 的 STUN/ICE 走 UDP 绕过代理直连,泄漏真实公网 IP
→ 一次泄漏永久关联所有「独立」Profile。

用户侧只想填 IP+账号密码,不想手填时区经纬度。

## Decision

**输入代理后,先通过候选代理查询真实出口 IP,再自动配置该 Profile 的:**

- 时区(Intl / Date)
- geolocation(经纬度)
- Accept-Language / navigator.languages

v1 使用 Provider 适配层。主服务为 FreeIPAPI,主服务超时、限流或返回无效数据时切换 IPWHOIS.IO。两个请求都必须经过候选代理,禁止 DIRECT 回退。返回值必须通过公网 IP、ISO 国家码、IANA 时区和经纬度校验。v1 不依赖或分发本地 GeoIP 数据库,因此没有数据库许可和更新分发要求;未来可以在 Provider 层切换付费服务或自建服务。

验证在独立、仅内存的 NetworkContext 中完成。成功后生成绑定当前草稿、5 分钟有效的一次性令牌。用户确认时先完成代理配置、凭证加密和所有 L3 数据准备,最后才启用代理。任一步失败均完整回滚,页面不能观察到半配置状态。

**同时强制 WebRTC 防漏:** Profile pref
`webrtc.ip_handling_policy=disable_non_proxied_udp`。禁用代理、代理冲突或配置移除时恢复用户原值。

时区使用 per-context/per-Isolate 的 Profile 基线。DevTools 时区覆盖是更高优先级的临时层; 清除 DevTools 覆盖后自动恢复 Profile 基线。禁止使用进程级 ICU 默认时区,否则并发 Profile 会互相污染。

代理每 15 分钟复检,并在启动、网络恢复和代理错误后复检。多窗口合并请求。策略或扩展接管代理时立即进入 conflict,恢复原语言、WebRTC、时区和定位,清除旧国旗;冲突解除后重新验证。

## Consequences

- 用户体验好:只填 IP+凭证,L3 一致性自动搞定。
- 免费 API 无 SLA 和永久可用保证,所以必须双 Provider、缓存、退避并显示 stale/error 状态。
- 不分发本地 GeoIP 数据库,避免数据库授权、体积和更新渠道成为 v1 交付阻断项。
- WebRTC 防漏代价:非代理 UDP 被禁 → 真 WebRTC 音视频通话会断。多号场景可接受。
- 这四项(时区/geo/语言/WebRTC)必须与代理**原子同步**,任一漏配 = 制造矛盾,比不加代理更糟。
- 密码仅以 OSCryptAsync 密文持久化;WebUI、日志和 QA 报告不得返回或记录明文凭证。
