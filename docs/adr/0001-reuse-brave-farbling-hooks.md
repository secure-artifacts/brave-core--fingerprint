# ADR-0001: 复用 Brave farbling 拦截点,替换其逻辑

Status: Accepted (2026-07-08)

## Context

从零在 Blink 里找到并挂钩每个指纹 API 是最难、最易漏的工作。Brave 的 farbling 系统
(隐私团队多年维护)已经把 ~20 个指纹面的拦截点全部建好——但它的逻辑是「per-session
随机加噪」,目的是**不可追踪**(每站每次都不同)。反检测浏览器目标相反:**稳定一致的假身份**。

拦截点(难且有价值)和加噪逻辑(与我们目标冲突)是可分离的两层。

## Decision

**复用 Brave farbling 的拦截点,替换里面的加噪逻辑为「返回本 Profile persona 的持久值」。**
`brave_session_cache` 的 PRNG 种子来源从「随机 token」改为「persona 派生」,使同一 Profile
跨 session 输出恒定、且取自真值池(见 ADR-0002)。

不把 farbling 当作要绕过的敌人,当作脚手架。

## Consequences

- 省掉重新定位所有 Blink 指纹钩子的巨量工作;拦截点已被 browsertest 覆盖。
- 需逐面把「加噪」换成「查 persona 值」,改造集中在 `chromium_src/.../` 的 override 文件。
- Brave **未挂钩**的面(maxTouchPoints、Gamepad、WebGL readPixels、整个 L3)仍需自建。
- 与上游 rebase 时,farbling 相关 override 文件是冲突高发区(上游会改这些文件)。
