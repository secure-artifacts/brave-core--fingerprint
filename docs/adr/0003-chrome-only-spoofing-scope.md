# ADR-0003: 只在 Chrome 系内伪装;v1 层范围 L1+L2+L3,L4 原生

Status: Accepted (2026-07-08)

## Context

跨引擎/跨 OS 伪装(Chrome 装 Safari、Win 装 Mac)听起来强,但会制造 L4 矛盾:
UA 说 Safari,而 TLS JA3/JA4、HTTP2
Akamai 指纹仍是 Chromium 的 → 检测器交叉核对秒穿。修 L4 是内核/BoringSSL 级工作(Very
Hard),基本是另一个项目。

## Decision

**v1 只在 Chrome 系内改**:UA 始终是 Chrome,只改版本号/OS 细节等 Chrome 内部维度。这样原版 Chromium 的 TLS/HTTP2/TCP 指纹**天然与 UA 一致**,L4 白拿,不用碰。

**v1 指纹层范围 = L1(JS取值)+ L2(渲染)+ L3(时区/geo)+ 代理 +
WebRTC 防漏。L4 保持原生。**

## Consequences

- 躲开跨引擎矛盾这个最常见的穿帮点,L4 零工作量还自洽。
- 真值池(ADR-0002)只需覆盖 Chrome-on-各OS 的真实组合,不需 Safari/Firefox 维度。
- 代价:无法伪装成非 Chrome 浏览器;若目标站专门稀释 Chrome 占比则受限(FB/Google 不这么做)。
- 防不住会做 TCP/IP(p0f)、JA4T 交叉核对的顶级检测器(DataDome/Akamai 深度模式)——明确列为 v1 范围外。
