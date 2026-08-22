## Context

在 Brave/Chromium fork（`fingerprint`
分支）上实现 v1 反检测指纹浏览器。域模型、术语、4 层指纹模型（L1-L4）、威胁模型、关键代码锚点见
`src/brave/CONTEXT.md`；6 个已锁定架构决策见
`src/brave/docs/adr/0001-0006`（本文的 Decisions 是它们的实现视角展开，不重复论证）。

现状：Brave
farbling 已挂钩 ~20 个指纹面（拦截点齐全，但逻辑是随机加噪，目标与我们相反）；Brave
Tor 已有完整 per-Profile 代理链路（可克隆）。这两点是本项目的最大杠杆——复用基建，而非从零。

约束：只在 `fingerprint` 分支改；上游 rebase 时 farbling
override 与 chromium_src 是冲突高发区；node 构建需 24.16.0；构建产物
`src/out/Component_arm64/`。

## Goals / Non-Goals

**Goals:**

- 每 Profile 一个稳定、自洽、真机存在的指纹身份（persona），覆盖 L1+L2+L3。
- per-Profile 代理（HTTP/HTTPS/SOCKS5
  CONNECT+认证）+ 验证后确认；按真实出口 IP 自动推导 geo；WebRTC 防漏；浏览器级国家状态入口。
- 1 身份 = 1 Chromium Profile，可并发、存储隔离。
- 最大化复用 Brave farbling 钩子与 Tor 代理链路。

**Non-Goals（v1 明确排除，写入 CONTEXT.md 范围外）:**

- L4 伪装（TLS JA3/JA4、HTTP2、TCP/IP）——保持原生 Chromium，靠 Chrome-only
  UA 自动一致。
- GPU 精确渲染伪装（ANGLE/驱动层复现目标显卡真实输出）——v1 用持久噪声替代。
- 养号拟人行为、批量 Profile 管理器、住宅代理采购（外部依赖）。
- SOCKS4、SOCKS5 BIND 与 UDP ASSOCIATE。

## Decisions

### D1 指纹替换：改种子来源，不改拦截点（ADR-0001）

farbling
token 的**真正来源**在 browser 侧：`components/brave_shields/core/browser/brave_shields_utils.cc`
的 `GetFarblingToken()`（当前 per-URL 随机、持久于 `BRAVE_SHIELDS_METADATA`
website-setting）。persona 派生须在此下沉，使 token 变为
**per-Profile 恒定、site-invariant**。
**根部原则（关键，避免下发路径 whack-a-mole）**：persona 派生 +
Container/nonce 变换的中和须下沉到**最低公共点** `GetFarblingToken()` 内部（在
`additional_entropy` XOR 之前），使**所有**下发路径与消费点自动收敛为该 Profile
persona token。否则须逐条改多条独立路径且极易漏。已知的
`default_shields_settings_`
喂入路径至少三条，各自独立重建 container-id/entropy：

- 文档导航：`browser/brave_shields/brave_shields_web_contents_observer.cc::SendShieldsSettings()`（`:317-360`，container-id 经
  `GetContainerIdForWebContents`）→ mojom `BraveShields.SetShieldsSettings` →
  `BraveContentSettingsAgentImpl`。
- **Dedicated
  Worker**：`worker_content_settings_client.cc::EnsureShieldsSettings()` → mojom
  `ContentSettingsManager.GetBraveShieldsSettings` →
  `ContentSettingsManagerImpl` → `content_settings_manager_delegate.cc` 的
  `GetBraveShieldsSettingsOnUI`（⚠️
  **此接口确有调用方**——纠正前述「无调用方」的错误说法）。
- **Shared/Service Worker**（含 MV3 扩展 background service
  worker）：`WorkerContentSettingsProxy.GetBraveShieldsSettings` →
  `BraveContentBrowserClient::WorkerGetBraveShieldSettings`（`:874-911`，第三个 container-id 派生
  `GetContainerIdFromStoragePartitionConfig`）。若不在根部收敛，Worker 内 OffscreenCanvas/AudioContext 的指纹会与主文档 persona 不一致。注意：仅改 renderer 侧
  `brave_session_cache.cc` 的 `MakePseudoRandomGenerator`
  **不够**——canvas/audio/ WebGL-extension 噪声直接读
  `default_shields_settings_->farbling_token`（`brave_session_cache.cc:288/306/345`），不经该函数；改造须覆盖 token 的所有直接消费点。各面 override（`base_rendering_context_2d.cc`、`webgl_rendering_context_base.cc`、`webaudio/audio_buffer.cc`、
  `navigator_base.cc`、`navigator_device_memory.cc`、`local_dom_window.cc`
  等）从「加噪」改为「查 persona」。
  **门控（逐面）**：persona 接管前须**按各面自己的 `ContentSettingsType`** 检查
  `GetBraveFarblingLevel(BRAVE_WEBCOMPAT_CANVAS/_AUDIO/_WEBGL/_FONT/...) == OFF`（非仅站点级
  `BRAVE_FINGERPRINTING_V2`），因逐面 webcompat 例外（`GetBraveWebcompatContentSettingFromRules`/`kBraveWebcompatExceptionsService`）可在站点级 BALANCED 时把单面强制 OFF；OFF（含逐面例外）时保留真实值。
  **连带**：`GetFarblingToken` 亦被
  `brave_reduce_language_network_delegate_helper.cc`（Accept-Language
  farbling）共享消费，改动须评估其影响并与 D6 的 geo 语言协调。
  **Profile 内分叉（关键）**：token 在源之外还被两处二次变换，会在同一 Profile 内分叉，破坏 persona 不变量，须一并处理——(a)
  `GetFarblingToken(additional_entropy)` XOR per-Container id（`kContainers`
  默认开于 Win/Mac）；(b) `brave_session_cache.cc:245-257` 构造时 XOR
  storage-key nonce（fenced frame/credentialless
  iframe 分区上下文）。方案：身份 Profile 内禁用 Containers 或令其 token 不因 container/nonce 分叉。
  _备选_：新建平行拦截层——否决，重复造轮 + 与上游冲突面更大。

### D2 Persona = 真值池合成 + 一致性引擎（ADR-0002）

真值池以结构化数据（JSON/资源）维护：真 UA+版本、各 OS
renderer 串、屏幕档、字体集、UA-CH、插件与 MIME 描述。一致性引擎按约束图（OS→UA→renderer→字体→屏幕→UA-CH）组装合法组合。persona 存于 per-Profile
pref。schema 原位迁移须保留 `persona_id`
与已有字段；损坏或未来版本数据不得静默换身份，改为回退 Brave 原生 farbling。媒体设备只映射实际存在的设备类型，每类最多一个泛化入口；`getUserMedia()`
在浏览器侧把泛化 `deviceId`
反向映射到真实设备。Gamepad 保留连接、按键与摇杆行为，只泛化硬件名称与标识。
_备选_：随机持久值（唯一性即破绽）/ 真机采集（需采集设施）/ 商业库（钱+合规）——均否决。

### D3 L2 渲染用持久噪声，非 GPU 精确伪装（ADR-0006）

复用 `PerturbPixels` / audio farbling helper，种子改 persona →
hash 稳定且 per-Profile 唯一。metadata 串（WebGL/WebGPU
vendor/renderer）另走 persona 真值。二者是两回事，都做。 *残余风险*见 Risks。

### D4 代理：Profile 服务 + Tor-first 注入（ADR-0004）

`chromium_src/chrome/browser/net/proxy_config_monitor.cc` 保持
`profile->IsTor()` 严格优先，普通 Profile 再读取 `FingerprintProxyService`
提供的 HTTP/HTTPS/SOCKS5 CONNECT 代理。策略或 `chrome.proxy`
扩展已控制代理时，本功能进入
`conflict`，不静默覆盖。已启用代理若配置或 Keychain 解密失败，返回本地阻断代理，MUST
NOT 临时 DIRECT。

`FingerprintProxyService` 是 Profile 级
`KeyedService`，统一管理草稿验证、加密凭证、应用配置、状态与定时复检。密码使用
`OSCryptAsync` 加密；WebUI 只暴露
`hasSavedPassword`，不读取明文。旧明文 pref 在加密器可用后迁移并清除；解密失败保持阻断状态。

### D5 设置页与浏览器状态入口（ADR-0004）

后端注册
`getState/verifyDraft/applyVerified/revalidate/disable`；前端为“输入 → 验证 → 查看结果并确认”三阶段。草稿任一字段变化或验证令牌过期都要求重新验证，失败不改变当前已生效代理。

工具栏在 VPN 后、Profile/App
Menu 前增加固定按钮：未配置为灰色代理图标加黄色警告，验证中显示进度，生效后显示内置国旗图集，过期加黄色徽标，连接失败使用语义红色错误图标，策略/扩展冲突显示受控状态。点击打开紧凑弹窗，完整编辑仍进入设置页。普通 Profile 支持；隐身继承原 Profile；Tor/Guest 不接管。

### D6 候选代理验证与 L3 原子联动（ADR-0005）

验证草稿时创建独立、仅内存
`NetworkContext`，仅配置候选代理、禁用 Cookie/缓存/重定向、无 DIRECT 回退。先经代理请求 FreeIPAPI，6 秒超时、429、网络错误或无效 JSON 时改请求 IPWHOIS.IO，再超时 6 秒。结果必须通过公网出口 IP、ISO 国家码、IANA 时区与坐标校验。

成功后生成绑定完整草稿的 5 分钟一次性验证令牌。验证期间锁定输入；草稿版本改变后丢弃旧异步结果。用户确认时先完成配置校验、凭证加密和全部派生设置准备，最后才启用代理；任一步失败完整回滚，页面不能观察到半配置状态。代理与以下派生设置同一次写入：

- **时区**：per-context/per-Isolate 覆盖。**排除**进程级 ICU 方案——`base/i18n/timezone.cc`
  的 `icu::TimeZone::createDefault()`
  是进程全局，并发 Profile 会互抢，直接违反「不同 Profile 时区互不影响」。Profile 时区作为基线；DevTools 临时覆盖优先，清除后恢复 Profile 基线。
- **Accept-Language**（pref
  `intl.accept_languages`）、**navigator.languages**：按代理国家；须处理 Strict 模式（`ControlType::BLOCK`）下
  `brave_reduce_language_network_delegate_helper.cc:147-151`
  硬编码 "en-US,en;q=0.9" 会盖掉 geo 语言的冲突。
- **geolocation**：生产级 per-Profile override（Profile 级 `GeolocationContext`
  拦截/权限层覆盖）；CDP `Emulation.setGeolocationOverride`
  仅 per-tab/DevTools 测试用，不作为生产机制。
- **WebRTC**（`webrtc.ip_handling_policy` =
  `disable_non_proxied_udp`）：该 pref 已由 brave://settings 暴露，覆盖前须保存用户原值、关代理时恢复原值（非硬回退 default）。四项与代理同一次生效（原子）。

每 15 分钟、浏览器启动、网络恢复或代理错误时复检；同一 Profile 多窗口合并请求。出口 IP 同国家变化时更新 IP 并提示；国家变化时同步更新时区、语言、坐标和国旗；API 临时失败保留上次配置并标记
`stale`；连接或认证失败标记 `error` 且继续禁止 DIRECT。

策略或 `chrome.proxy` 扩展接管代理时立即进入
`conflict`，恢复原语言、WebRTC、时区与定位并清除旧国旗；冲突解除后重建安全 WebRTC 设置并重新验证，未确认前继续 fail-closed。

v1 使用 Provider 适配层：FreeIPAPI 为主、IPWHOIS.IO 为备用，不依赖本地 GeoIP 数据库。免费服务无 SLA；保留 Provider 接口，未来可换付费服务或自建服务。

### D7 身份隔离 = Profile（ADR-0004）

不新建隔离层：Chromium Profile 天然隔离 storage +
NetworkContext。persona/proxy 均挂 Profile pref。并发多 Profile
= 多 NetworkContext 同时存活（原生支持）。

### D8 `IsTor()` 防泄漏门控审计

5.8 审计结论：只扩展会绕过 Profile proxy 的网络/本地发现保护；Tor
UI、Tor 导航、Tor 搜索、窗口外观、统计、password/autofill 私密窗口语义等 Tor 身份专属门控不扩到普通代理身份。

- `brave/chromium_src/chrome/browser/media/router/media_router_feature.cc`：扩展到
  `fingerprint_browser::ShouldUseProfileProxy()`，启用 profile proxy
  identity 时禁用 Media Router/Cast/DIAL/SSDP 本地设备发现。
- `brave/browser/net/brave_ad_block_tp_network_delegate_helper.cc`：CNAME
  uncloaking 会发额外 DNS 查询，原逻辑只跳过 Tor 和标准 proxy pref；profile
  proxy identity 走独立 pref，因此在 `ProxySettingsAllowUncloaking()`
  中补同等门控，避免 DNS 查询绕过代理。
- `brave/browser/geolocation/brave_geolocation_permission_context_delegate.cc`：Tor 直接拒绝 geolocation；身份 Profile 不复用拒绝策略，按 6.5 做代理 geo 坐标 override。
- `brave/chromium_src/chrome/browser/speech/chrome_speech_recognition_manager_delegate.cc`、password/autofill/page-info/UI/metrics/onion相关
  `IsTor()`：不是代理绕过型本地/网络发现，记录为 v1 5.8 范围外。

## Risks / Trade-offs

- **L2 噪声 hash ≠ 声称显卡真值** → 遇到持「GPU→canvas
  hash」映射库的检测器可能穿帮。缓解：v1 接受此残余风险（FB 是否有此库未知），实测被 flag 再升级 GPU 精确伪装（Non-Goal 转正）。
- **上游 rebase 冲突** → farbling
  override 与 chromium_src 是上游高频改动区。缓解：改动尽量集中在种子派生点与 persona 查表，减少对 override 逻辑主体的侵入；每次 rebase 重点复查这些文件。
- **真值池过小 →
  persona 撞车**（crowd-blending 双刃：太独特/太重复都糟）→ 缓解：池子分层扩充，监控分配去重。
- **WebRTC `disable_non_proxied_udp` 断真 WebRTC 通话**
  → 多号场景可接受；文档标注。
- **时区 per-Profile override 落地方式未定**（ICU 进程级 vs
  per-context）→ 见 Open Questions。
- **免费 IP 地理服务可用性/限额/条款变化**
  → 双 Provider、6 秒超时、缓存状态与退避；首次验证两者均失败则禁止应用，已生效代理保留上次指纹并标记过期。Provider 适配层允许后续替换付费或自建服务。
- **IP 地理定位精度有限**
  → 明确为近似位置；结果仍须通过国家码、IANA 时区、坐标和公网 IP 校验。
- **指纹一致性≠账号存活**（FB 还看行为/IP 信誉）→ 已在 CONTEXT.md 与 proposal 明示，范围诚实。
- **破坏 Brave 原生隐私 farbling / Tor 代理**（改 `GetFarblingToken`、扩
  `IsTor()` 缝）→ 缓解：persona 缺失回退原随机、Tor-first 分支优先、per-site
  OFF 门控保留；加回归 browsertest（任务 9.5）。
- **逐站点「指纹保护 OFF」被 persona 静默覆盖** → 缓解：接管前检查 farbling
  level==OFF，OFF 保留真实值（任务 2.3）。
- **WebRTC/Accept-Language 共享 pref 被静默改写**（用户原设值丢失、Strict 硬编码覆盖 geo 语言）→ 缓解：存/恢复原值、协调 Strict 分支（任务 6.4/6.6）。
- **Local Font Access API 漏宿主字体**（`navigator.fonts.query()`
  无 override）→ 缓解：按 persona 字体集过滤/屏蔽（任务 4.5）。

## Migration Plan

- 全部改动在 `fingerprint` 分支；`master` 保持 upstream 纯镜像。
- 分阶段（对应 tasks 分组）：persona 数据模型 → 指纹面接入 → 代理链路 →
  L3 联动 → 设置 UI → 隔离/并发验证。
- 每阶段用指纹检测页（CreepJS/FingerprintJS/browserleaks）+ 真实代理实测自证。
- 回退：改动可按 capability 分组 revert；farbling 种子派生点保留「persona 缺失则回退原 farbling」的安全兜底。

## Open Questions

- 时区 per-Profile
  override 的具体实现：已**排除**进程级 ICU 方案（与并发冲突）；余下在 per-context
  ICU override vs 类 CDP
  Emulation 的 renderer 注入间选，设计实现阶段定，spec 只约束可观测行为（JS 时区 = 代理时区且并发 Profile 互不影响）。
- geolocation 生产覆盖的具体挂载点：Profile 级 `GeolocationContext`
  拦截 vs 权限层坐标覆盖——实现阶段定。
- Geo
  Provider 后续是否切换付费/自建服务；v1 已确定 FreeIPAPI 主用、IPWHOIS.IO 备用，不以离线数据库采购或更新分发作为交付前提。
- 真值池初始数据来源与规模：先小池覆盖 Chrome-on-Win/Mac 主流机型，逐步扩。
- persona 缺失/损坏时的兜底：回退原 farbling 随机（安全但失去一致性）——已定倾向前者，写入任务 2.5。
