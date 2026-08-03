## 1. Persona 数据模型与真值池（fingerprint-persona）

- [x] 1.1 定义 persona
      schema（L1/L2 全字段：UA、UA-CH、platform、hardwareConcurrency、deviceMemory、languages 基线、屏幕档、maxTouchPoints、WebGL
      vendor/renderer、WebGPU adapter、canvas/audio 噪声种子、字体集）
- [x] 1.2 建真值池数据集（结构化资源）：真 UA+版本、各 OS
      renderer 串、屏幕档、字体集、UA-CH，先覆盖 Chrome-on-Win/Mac 主流机型
- [x] 1.3 实现一致性引擎：按约束图（OS→UA→renderer→字体→屏幕→UA-CH）组装合法自洽组合；真值池不足时报错不填充
- [x] 1.4 persona 持久化到 per-Profile pref；实现读写与版本迁移
- [x] 1.5 Profile 创建钩子：自动合成并绑定 persona
- [x] 1.6 单测：一致性引擎产出无跨面矛盾；重启后 persona 不变；不同 Profile 不同 persona

## 2. 指纹替换的种子来源与门控（fingerprint-spoofing，最关键）

- [x] 2.1 persona 派生 token 下沉到**真正来源**：browser 侧
      `components/brave_shields/core/browser/brave_shields_utils.cc` 的
      `GetFarblingToken()`（当前 per-URL 随机、持久于 BRAVE_SHIELDS_METADATA
      website-setting）；使 token 变为 **per-Profile 恒定、site-invariant**
- [x] 2.1a **根部收敛原则**（关键）：persona 派生 +
      Container/nonce 中和须下沉到最低公共点 `GetFarblingToken()`
      内部（`additional_entropy` XOR 之前），使**所有**
      `default_shields_settings_`
      喂入路径自动收敛，避免逐路径漏改。已知至少三条独立路径（各自重建 container-id）：(1) 文档导航
      `brave_shields_web_contents_observer.cc::SendShieldsSettings()`；(2)
      Dedicated Worker →
      `content_settings_manager_delegate.cc::GetBraveShieldsSettingsOnUI`（⚠️
      **此接口确有调用方**，纠正前述「无调用方」）；(3) Shared/Service
      Worker（含 MV3 扩展 SW）→
      `brave_content_browser_client.cc::WorkerGetBraveShieldSettings`（`:874-911`）。若不在根部收敛，Worker 内 OffscreenCanvas/AudioContext 指纹会与主文档 persona 不一致
- [x] 2.2 说明：仅改 renderer 侧 `brave_session_cache.cc` 的
      `MakePseudoRandomGenerator`
      **不够**——canvas/audio/WebGL-extension 噪声直接读
      `default_shields_settings_->farbling_token`（`brave_session_cache.cc:288/306/345`），不走该函数；改造须覆盖
      `farbling_token` 的所有直接消费点
- [x] 2.3 门控保留须**逐面**（非站点级）：persona 接管前须按各面自己的
      `ContentSettingsType` 检查
      `GetBraveFarblingLevel(BRAVE_WEBCOMPAT_CANVAS/_AUDIO/_WEBGL/_FONT/...) == OFF`，而非单一站点级
      `BRAVE_FINGERPRINTING_V2`；因 per-site per-face
      webcompat 例外（`GetBraveWebcompatContentSettingFromRules`，`kBraveWebcompatExceptionsService`）可在站点级仍 BALANCED 时把单面强制 OFF。OFF（含逐面例外）时保留真实值，不施加 persona
- [x] 2.4 评估改 `GetFarblingToken` 对 Accept-Language
      farbling 的连带影响：`brave/browser/net/brave_reduce_language_network_delegate_helper.cc`
      共享同一 token 源，须确保不破坏其行为或与 geo 语言（任务组 5）显式协调
- [x] 2.5 加「persona 缺失/损坏则回退原 farbling 随机」安全兜底
- [x] 2.6 处理
      **Containers 功能对 token 的 Profile 内分叉**：`GetFarblingToken(additional_entropy)`
      把 per-Container
      id（`containers::GetContainerIdForWebContents`，`kContainers`
      默认开于 Win/Mac）XOR 进 token（`brave_shields_utils.cc:884-891`）→ 同 Profile 不同 Container 标签页 token 分叉，破坏 persona 不变量。方案：身份 Profile 内禁用 Containers，或使 container 场景 token 仍等于该 Profile
      persona 值（不因 container
      id 分叉）；补 browsertest 覆盖同 Profile 跨 Container 的 canvas/WebGL/Accept-Language 一致
- [x] 2.7 处理 **storage-key nonce XOR**：`brave_session_cache.cc:245-257`
      构造时把 `farbling_token` 与 storage-key nonce 哈希 XOR（fenced
      frame/credentialless
      iframe 等分区上下文，在 canvas/audio/webgl 消费点之前）→
      persona 接管后须确认该分区变换移除或另行协调，避免分区上下文 token 与 Profile
      persona 不一致

## 3. 指纹面接入 L1（fingerprint-spoofing）

- [x] 3.1
      navigator 面 override 改查 persona：`navigator_base.cc`（UA/hardwareConcurrency）、`navigator_device_memory.cc`、`navigator_language.cc`、`dom_plugin_array.cc`
- [x] 3.2
      UA-CH 接 persona：`GetUserAgentMetadata`（`brave_content_browser_client.cc`）保证 UA 与 UA-CH 一致
- [x] 3.2a 覆盖**真正发到网络的 `User-Agent:` HTTP 头**：出自
      `embedder_support::GetUserAgent()` /
      `ChromeContentBrowserClient::GetUserAgent()`，与 JS
      getter 是**独立路径**、当前对 Brave 未覆盖（`brave_navigator_useragent_farbling_browsertest.cc:298-382`
      证明各级别 wire 头 = 真实未 farble
      UA）。须使 wire 头与 persona 一致，否则每个请求服务端可见真实宿主 UA→秒穿。注意
      `GetUserAgent()`/`GetUserAgentMetadata()` 签名无 Profile 参数。persona
      UA 覆盖 SHALL **仅**在 per-Profile 的
      `ProfileNetworkContextService::ConfigureNetworkContextParams` 处对
      `NetworkContextParams.user_agent` 做 profile-specific 覆写（在共享的
      `ConfigureDefaultNetworkContextParams()` 之后），**MUST NOT** 改
      `embedder_support::GetUserAgent()`/`ChromeContentBrowserClient::GetUserAgent()`
      本身——该函数被 profile-无关的 **System
      NetworkContext**（`SystemNetworkContextManager`，服务不关联 Profile 的请求）共享，改它会污染整进程系统流量 UA、殃及非 persona/Tor
      Profile。加回归 browsertest：有 persona 时 System
      NetworkContext（不关联 Profile 的请求）仍报真实未 spoof UA
- [x] 3.3
      screen/window 面接 persona：`local_dom_window.cc`、`media_values.cc`（FarbleInteger 改 persona 值）
- [x] 3.4 补齐未挂钩面：navigator.maxTouchPoints、Gamepad、WebGL
      readPixels 新增拦截并接 persona
- [x] 3.5 补齐鼠标/指针/触摸事件 screenX/screenY 面：`chromium_src/.../core/events/mouse_event.h`、`pointer_event.h`、`core/input/touch.h`（`FarbleKey::kPointerScreenX/kPointerScreenY`，经
      `FarbledPointerScreenCoordinate`，现逻辑丢弃真实值改用 clientX/Y+[0,8]
      偏移）——须与 3.3 的 persona 窗口位置可推导一致，避免
      `event.screenX - clientX` 与 `window.screenX` 矛盾
- [x] 3.6
      browsertest：navigator/screen/UA-CH 各值等于 persona 且互相一致；`event.screenX/screenY`
      与 `window.screenX/screenY` 自洽

## 4. 指纹面接入 L2（fingerprint-spoofing）

- [x] 4.1
      canvas 噪声种子改 persona：`base_rendering_context_2d.cc`、`html_canvas_element.cc`、`canvas_async_blob_creator.cc`（PerturbPixels 种子；注意 OFF 门控见 2.3）
- [x] 4.2 audio 噪声种子改 persona：`webaudio/audio_buffer.cc` +
      `brave_audio_farbling_helper.cc`
- [x] 4.3 WebGL/WebGPU
      metadata 串接 persona 真值：`webgl_rendering_context_base.cc`、`webgl2_rendering_context_base.cc`、`modules/webgpu/gpu_adapter.cc`
- [x] 4.4
      CSS/measureText 字体面接 persona 字体集：`local_font_face_source.cc`、`brave_font_whitelist.cc`、measureText 门控
- [x] 4.5 Local Font Access
      API 门控：`third_party/blink/renderer/modules/font_access/font_access.cc`/`font_metadata.cc`
      的 `navigator.fonts.query()`（已 shipped，当前无 Brave
      override，会枚举宿主真实字体）——按 persona 字体集过滤或屏蔽
- [x] 4.6 WebUSB `serialNumber()`（`usb_device.cc`，经
      `GenerateRandomString(farbling_token)`，是又一 token 直接消费点）接 persona 或确认 persona 接管后仍 per-Profile 恒定；补 browsertest 或在 CONTEXT.md 锚点列表记录
- [x] 4.7 审计并接入 persona 的另两个已挂钩面：`mediastream/media_devices.cc`（`enumerateDevices`
      只 shuffle，真实 label/count 未按 persona 过滤）、`speech/speech_synthesis.cc`（BALANCED 仍透出真实已装 TTS 语音列表，可能与 persona
      OS/locale 矛盾）——比照 4.5/4.6；persona schema（1.1）相应加字段
- [x] 4.8 browsertest：同 Profile canvas/audio hash 稳定；不同 persona
      hash 不同；renderer 串为真值池真实且与 OS 兼容；`navigator.fonts.query()`
      不泄漏宿主字体；media devices/speech voices 与 persona 自洽

## 5. per-Profile 代理链路（profile-proxy）

- [x] 5.1 扩展 `chromium_src/chrome/browser/net/proxy_config_monitor.cc`
      注入缝：新增通用 per-Profile 代理判断，且**严格保持 `profile->IsTor()`
      分支优先**（Tor-first），避免 Tor
      Profile 落到通用服务丢失 circuit 认证/轮换
- [x] 5.1a 定义新 per-Profile 代理与既有
      **扩展代理（`chrome.proxy`）/企业策略代理（`prefs::kProxy`）**
      的优先级：扩注入缝会绕过标准
      `CreateProxyConfigService`（该路径也承载扩展/策略代理，Tor 因不跑扩展才安全，新身份 Profile 无此前提）；至少当扩展/策略已管控该 Profile 代理时给出可见警告而非静默覆盖
- [x] 5.2 实现 Profile 级 `FingerprintProxyService` 并接入
      `proxy_config_monitor.cc`，承载 HTTP/HTTPS server + 凭证；Tor-first
- [x] 5.3 收口 v1 协议范围：设置页仅提供 HTTP/HTTPS，产品服务在网络请求前拒绝 SOCKS5；底层 SOCKS5 代码保留但不启用、不纳入交付验收
- [x] 5.4 HTTP 代理认证无弹窗：NetworkContext 建立时预填 `HttpAuthCache` 或直调
      `LoginHandler::SetAuth()`
- [x] 5.5 per-Profile pref 承载代理配置与状态；密码通过 `OSCryptAsync`
      加密，WebUI 仅返回 `hasSavedPassword`，迁移并清除旧明文 pref
- [x] 5.6 browsertest：Profile
      A 走代理、B 直连互不影响；认证无弹窗；重启后代理保持；**Tor
      Profile 行为不受注入缝扩展影响**
- [x] 5.7 检测运行时代理认证失败（HTTP 407 反复失败）并经设置页给出可见错误反馈（对应 spec「认证失败可见反馈」，区别于 7.3 的静态字段校验）；补 browsertest
- [x] 5.8 审计其他
      `IsTor()`-gated 保护是否应扩展到 per-Profile 代理身份（非 Tor 但需同类防泄漏）：如
      `media_router_feature.cc:19-22` 在 Tor 窗口禁用 Media
      Router/Cast 本地设备发现（否则本地网络设备发现绕过代理=设备图谱泄漏，撞 profile-identity「一方泄漏不关联另一方」）；逐点决定加同等门控或显式记录为范围外
- [x] 5.9 实现 fail-closed：代理连接/认证失败、保存配置损坏或 Keychain 解密失败时使用阻断代理，MUST
      NOT DIRECT；日志与 QA 报告不记录凭证

## 6. 代理 geo 一致性 L3（proxy-geo-consistency）

- [x] 6.1 实现 Geo
      Provider 适配层：FreeIPAPI 主用、IPWHOIS.IO 备用；通过候选代理查询并严格校验公网 IP、ISO 国家码、IANA 时区和坐标
- [x] 6.2 时区 per-Profile
      override：**排除进程级 ICU 方案**（`base/i18n/timezone.cc` 的
      `createDefault()`
      是进程全局，并发 Profile 会互抢，违反「不同 Profile 时区互不影响」）；改用 per-context/per-Isolate 覆盖机制
- [x] 6.3 Accept-Language（`intl.accept_languages`）+
      navigator.languages 按代理国家推导
- [x] 6.4 处理 Strict 模式冲突：`brave_reduce_language_network_delegate_helper.cc`
      在
      `ControlType::BLOCK`（Strict 指纹保护）下硬编码 Accept-Language="en-US,en;q=0.9"（`:147-151`），会盖掉 geo 推导语言——须协调或显式二选一，避免重新引入 IP/语言矛盾
- [x] 6.5 geolocation 生产级 per-Profile override：CDP
      `Emulation.setGeolocationOverride`
      仅 per-tab/DevTools 测试用，不适用普通浏览；需实现 Profile 级
      `device::mojom::GeolocationContext` 拦截或权限层覆盖，普通页面
      `navigator.geolocation` 即返回代理坐标
- [x] 6.6 强制 WebRTC `disable_non_proxied_udp`（pref
      `webrtc.ip_handling_policy`）随代理启停；**代理开启前先保存用户原有 pref 值，关闭时恢复该值而非硬回退 default**（该 pref 已由 brave://settings 隐私项暴露给用户）
- [x] 6.7 实现“验证后确认”：独立仅内存 NetworkContext 禁止 DIRECT 回退；验证成功生成绑定草稿的 5 分钟一次性令牌；确认后代理+时区+geo+语言+WebRTC 同一次生效
- [x] 6.8
      Accept-Language/navigator.languages 存/恢复：与 WebRTC（6.6）同类共享 user-editable
      pref，覆盖前保存用户原有
      `intl.accept_languages`、关代理时恢复，避免永久覆盖用户手设语言
- [x] 6.9 明确 v1 Provider/许可与分发策略：使用免费 HTTPS
      API，不依赖或分发本地 GeoIP 数据库；保留 Provider 适配层供未来切换付费/自建服务
- [ ] 6.10
      browsertest/实测：JS 时区=代理时区且并发 Profile 互不影响；WebRTC 无真实 IP
      srflx；切换代理四项同步；关代理后语言/WebRTC 恢复用户原值
- [x] 6.11 实现 15 分钟、浏览器启动、网络恢复和代理错误复检；多窗口合并请求；覆盖同国 IP 变化、国家变化、stale 与 error 状态

## 7. 设置 UI（profile-proxy）

- [x] 7.1 后端 handler 接入 Profile 服务并提供
      `getState/verifyDraft/applyVerified/revalidate/disable`
- [x] 7.2 注册 handler 于 `brave/browser/ui/webui/brave_settings_ui.cc`
- [x] 7.3 前端设置页实现“输入代理 → 验证代理 → 查看结果并确认应用”三阶段，覆盖 HTTP/HTTPS、凭证和字段校验
- [x] 7.4 前后端联动：草稿变化/令牌过期/重复确认拒绝；显示出口、国家/城市、时区、坐标、语言与 WebRTC 状态
- [ ] 7.5 chrome-mcp 实测截图自证：填代理→保存→开关→页面指纹/IP/时区正确
- [x] 7.6 工具栏在 VPN 后增加固定状态按钮和弹窗；本地 flag-icons 图集；未配置、验证中、active、stale、error、conflict 状态适配深浅主题
- [x] 7.7 提供独立 `brave://settings/fingerprintProfileProxy`
      路由、隐私页入口和工具栏配置直达；browser test 与原生 QA 点击覆盖两条入口

## 8. 身份隔离与并发（profile-identity）

- [x] 8.1 确认 persona+proxy 均绑定 Profile；创建时分配、销毁时清理绑定与隔离存储
- [x] 8.2 定义并实现「有 persona 无代理」降级：L1/L2 走 persona，提示地理不一致风险
- [x] 8.3 多 Profile 并发验证：同时运行两身份，指纹/代理/存储互不串号
- [x] 8.4 browsertest：并发身份 cookie/localStorage 隔离；一方泄漏不关联另一方

## 9. 端到端验证与文档

- [ ] 9.1 全量指纹检测页扫描（CreepJS/FingerprintJS/browserleaks）：无 lied/mismatch
- [ ] 9.2 真实代理下 geo 一致性实测：IP 国家=时区=语言=geolocation
- [ ] 9.3 WebRTC 泄漏专项测试：确认无真实公网 IP
- [x] 9.4 回归：确认 L4（TLS）与原版 Chromium 一致、未被改动
- [x] 9.5 回归：非 persona/普通 Profile 的 Brave 原生 farbling 隐私保护不被破坏；Tor
      Profile 代理正常
- [x] 9.5a Worker 一致性回归：Dedicated/Shared/Service
      Worker（含 MV3 扩展 background service
      worker）内 canvas(OffscreenCanvas)/audio/WebGL 指纹与主文档同 persona 一致；wire
      `User-Agent` 头 = persona（用能观测网络头的测试，非纯客户端 JS 扫描）
- [x] 9.6 更新 CONTEXT.md 代码锚点（`GetFarblingToken`
      为 token 真正来源；geolocation 生产覆盖机制；若实现中路径有出入）
- [x] 9.7 更新根目录 CLAUDE.md：`brave_session_cache`/farbling 描述从「per-session
      per-origin 随机」改为「persona 派生、缺失才回退随机」；把 fingerprint 分支的「sayHello 注入」描述更新为 persona/proxy/L1-L3 系统（同步
      `docs/agents/domain.md`）

## 10. 阻断式自动化 QA

- [x] 10.1 实现 `tools/fingerprint_browser/qa/run_qa.mjs`
      的 Smoke/Full/Soak 分层门禁与 JSON/Markdown/JUnit 报告
- [x] 10.2 启动前验证源码新鲜度、完整 dylib 集合 UUID/版本、libchrome/resource 哈希、四类 scaled
      resource pack、完整 locale 集与 codesign；仅操作唯一
      `/tmp/fingerprint-browser-*` 进程树
- [x] 10.3 Full 接入 persona/proxy/farbling/WebUI C++ 测试、`net_unittests`
      HTTP 代理认证回归、Profile/代理/MV3/CWS 生命周期和第三方扫描证据
- [x] 10.4 实现浅色/深色与三窗口尺寸截图矩阵、WCAG/布局/纯红连通域检查、批准 baseline 和产物绑定人工复核门禁
- [x] 10.5 实现 60 分钟三 Profile/20 标签/200 导航/20 代理切换/10
      Profile 周期/10 扩展周期 Soak，逐分钟采集进程/RSS/CDP 健康
- [ ] 10.6 提供真实 HTTP `0600` fixture、批准 baseline/native evidence 后执行 Full 与 Soak 并归档 0 失败报告；Chrome Web Store 安装由用户人工确认通过，不再要求原崩溃插件 URL
