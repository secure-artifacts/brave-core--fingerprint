## Why

我们要在 Brave/Chromium 基础上做一个反检测（antidetect）指纹浏览器，用于广告/社媒多号运营（FB
/ Google
Ads）。核心矛盾：每个账号需要在一台机器上呈现一个稳定、自洽、像真机的独立身份，否则被指纹关联/封号。Brave 现有的 farbling 系统已经把 ~20 个指纹面的拦截点建好，但其逻辑是「随机加噪求不可追踪」，与我们「稳定一致假身份」的目标相反——可复用其钩子、替换其逻辑。完整域模型与决策见
`src/brave/CONTEXT.md` 与 `src/brave/docs/adr/0001-0006`。

## What Changes

- **复用 Brave farbling 拦截点，替换逻辑**：`brave_session_cache`
  的 PRNG 种子来源从随机 token 改为 per-Profile
  persona 派生，使指纹输出稳定、取自真值池（ADR-0001）。
- **新增 persona 系统**：真值池（真实存在的 WebGL
  renderer 串 / 各 OS 屏幕档 / 字体集 /
  UA+版本）+ 一致性引擎（拼出合法自洽组合）+ per-Profile 持久化（ADR-0002）。
- **覆盖指纹层 L1+L2+L3**：L1（navigator/screen/UA-CH）、L2（canvas/webgl/webgpu/audio 用 per-Profile 持久噪声 +
  metadata 取真值池）、L3（时区/geo/语言）。补齐 Brave 未挂钩的面（maxTouchPoints/Gamepad/WebGL
  readPixels）。
- **只在 Chrome 系内伪装**（ADR-0003）：UA 始终 Chrome → 原版 Chromium
  TLS/HTTP2（L4）自动与 UA 一致，L4 保持原生、不改。
- **新增 per-Profile 代理**：HTTP/HTTPS + 账号密码，沿用 Tor 的 per-Profile
  `ProxyConfigService` 注入模式（ADR-0004）。凭证使用 `OSCryptAsync`
  加密，代理不可用时禁止 DIRECT 回退。
- **新增验证后确认流程**：在独立、仅内存的 `NetworkContext`
  中经候选代理请求 FreeIPAPI，失败时切换 IPWHOIS.IO；返回真实出口 IP、国家、城市、IANA 时区与坐标。验证结果绑定当前草稿并生成 5 分钟一次性令牌，只有用户确认后才应用代理与指纹。
- **代理 geo 一致性**：确认代理后原子设置时区/经纬度/Accept-Language（L3）并强制 WebRTC 防漏
  `disable_non_proxied_udp`；每 15 分钟及网络恢复时复检出口。
- **浏览器级状态入口**：VPN 后、Profile/App
  Menu 前增加固定代理按钮；未配置显示黄色警告，生效后持续显示出口国家国旗，过期、错误和策略冲突使用独立语义状态。
- **身份隔离**：1 身份 = 1 Chromium
  Profile，可并发；persona 与 proxy 均绑定到 Profile（ADR-0004）。
- **明确排除 v1**：SOCKS5 产品支持、L4（TLS/HTTP2/TCP 伪装）、GPU 精确渲染伪装、养号行为、批量 Profile 管理器。现有底层 SOCKS5 代码保留，但产品服务不启用且不纳入交付验收。

## Capabilities

### New Capabilities

- `fingerprint-persona`:
  persona 数据模型——真值池、一致性引擎、per-Profile 生成与持久化、生命周期。
- `fingerprint-spoofing`: 把 persona 值施加到 L1+L2 指纹面（navigator/screen/UA-CH/canvas/webgl/webgpu/audio +
  Brave 未挂钩面），复用/替换 farbling 钩子；Chrome-only 范围，L4 原生。
- `profile-proxy`:
  per-Profile 代理配置（HTTP/HTTPS + 认证）、验证后确认状态机、加密凭证、设置页与工具栏状态按钮。
- `proxy-geo-consistency`: 通过候选代理查询 FreeIPAPI/IPWHOIS.IO，按真实出口 IP 推导时区/经纬度/Accept-Language（L3）+
  WebRTC 防漏，并定时复检。
- `profile-identity`: 1 身份 = 1 Chromium Profile 的隔离与并发；persona ↔ proxy
  ↔ Profile 的绑定与生命周期。

### Modified Capabilities

<!-- openspec/specs/ 当前为空，无既有 capability 需改。 -->

## Impact

- **Renderer /
  Blink**：`third_party/blink/renderer/core/farbling/brave_session_cache.{h,cc}`
  及 `chromium_src/.../`
  下各指纹面 override（canvas/webgl/webgpu/audio/navigator/screen/font）。
- **Net
  / 代理**：`chromium_src/chrome/browser/net/proxy_config_monitor.cc`（Tor-first 注入缝）、Profile 级
  `FingerprintProxyService`、独立验证 `NetworkContext`、HTTP/HTTPS 代理认证链路。
- **L3 覆盖点**：`base/i18n/timezone.cc`（时区）、pref
  `webrtc.ip_handling_policy` / `--force-webrtc-ip-handling-policy`、pref
  `intl.accept_languages`、geolocation override。
- **设置 UI**：`brave/browser/ui/webui/settings/`、`brave/browser/resources/settings/`、`brave/browser/ui/webui/brave_settings_ui.cc`，以及浏览器工具栏固定状态按钮/弹窗。
- **外部服务与资源**：FreeIPAPI（主）与 IPWHOIS.IO（备用）Provider 适配层、固定版本 MIT
  `flag-icons` 图集、真值池数据集。v1 不分发本地 GeoIP 数据库。
- **Profile / prefs**：新增 per-Profile prefs 承载 persona 与 proxy 配置。
- **上游同步风险**：farbling override 文件与 chromium_src 是 rebase 冲突高发区。
