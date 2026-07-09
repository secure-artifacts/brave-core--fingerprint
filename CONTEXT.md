# CONTEXT — 指纹浏览器项目

本文件是这个 fork(`secure-artifacts/brave-core--fingerprint`,`fingerprint` 分支)
的项目域模型与统一语言(ubiquitous language)。设计讨论、bug 诊断、TDD 等 skill
在探索代码前应先读本文件,使用这里定义的术语,不要漂移到同义词。

## 项目目标

在 Brave/Chromium 基础上做一个反检测(antidetect)指纹浏览器,让每个 Profile 呈现
一个稳定、自洽、像真机的假身份,用于**广告/社媒多号运营(FB / Google Ads)**。
用户侧只需两件事:①(自动)更换浏览器指纹;② 一个 IP 输入页(含账号密码)+ 启动开关,
让当前 Profile 走该代理。

## 威胁模型与范围边界

目标检测器是消费级最狠的一档(FB/Google):指纹 + 行为 + 设备图谱 + IP 信誉 + ML 聚类。

**关键认知:指纹一致性是地板,不是天花板。** FB 多号存活靠 4 根支柱,本项目只覆盖第 1 根:

1. 指纹 + 代理一致性 ← **本项目(必要非充分)**
2. 住宅/移动代理(非数据中心)← 运营采购,浏览器管不了,但缺它前面全白搭
3. 养号拟人行为 ← v1 范围外
4. 注册/支付卫生 ← 运营

### 指纹面分 4 层(难度阶梯)

- **L1 JS/Blink 取值**:navigator.*、screen.*、UA-CH 等。Easy。Brave 多数已挂钩。
- **L2 渲染/GPU/音频**:Canvas、WebGL、WebGPU、AudioContext、字体。Hard(渲染字节)。Brave 已挂钩。
- **L3 时区/地理/语言**:Intl timezone、geolocation、Accept-Language。Medium,**必做**。Brave **没**挂钩。
- **L4 网络层**:TLS JA3/JA4、HTTP2、TCP/IP、WebRTC。Very Hard(内核级)。Brave 原版未改。

**v1 范围 = L1 + L2 + L3 + 代理 + WebRTC 防漏。L4 保持原生**(见 ADR-0003:UA 始终 Chrome
→ 原版 Chromium 的 TLS/HTTP2 自动与 UA 一致,L4 白拿,避开跨引擎矛盾)。

## 核心架构决策(详见 docs/adr/)

- **ADR-0001** 复用 Brave farbling 的**拦截点**,替换其**逻辑**(加噪→返回 persona 持久值)
- **ADR-0002** persona = 真值池合成,per-profile 持久
- **ADR-0003** 只在 Chrome 系内伪装;v1 层范围 L1+L2+L3,L4 原生
- **ADR-0004** 1 身份 = 1 Chromium Profile,可并发;代理走 per-Profile ProxyConfigService(仿 Tor)
- **ADR-0005** 代理 geo 按 IP 自动推导 + WebRTC 防漏
- **ADR-0006** L2 渲染用 per-profile 持久确定性噪声(非 GPU 精确伪装)

## 统一语言(术语表)

- **Persona**:一个 Profile 绑定的合成指纹身份。由真值池按一致性规则拼出的一套自洽值
  (UA + 显卡串 + 屏幕档 + 字体集 + 时区 + …),同 Profile 跨 session 恒定。
- **Profile**:一个 Chromium Profile = 一个身份单元,自带独立 storage + NetworkContext +
  代理 + persona。多号 = 多 Profile 并发。区别于「浏览器窗口」(同 Profile 多窗口共享身份)。
- **Farbling**:Brave 现有的**防护**机制——per-session/per-eTLD+1 随机加噪,目的是**不可追踪**。
  本项目目标相反(稳定一致假身份),故**复用其拦截点、替换其逻辑**。别把 farbling 当敌人,当脚手架。
- **真值池(real-value pool)**:人工维护的、真实存在的候选值集合(真 WebGL renderer 串、各 OS
  真屏幕档、各 OS/locale 真字体集、真 UA+版本)。persona 从池里按规则组装,保证组合真机存在。
- **一致性 / crowd-blending**:检测器不单看单值,看各信号是否互相自洽 + 是否跟出口 IP 一致,且是否
  在真实人群中常见。随机唯一值 = 反而是破绽。矛盾示例:UA=Windows 但 renderer=Apple M2;IP 在
  德国但 timezone=Asia/Shanghai;desktop UA 但 maxTouchPoints>0。
- **代理绑定**:一个 Profile → 一个上游代理(HTTP/SOCKS5 + user/pass)+ 由该代理 IP 自动推导的
  timezone/经纬度/Accept-Language + WebRTC 防漏策略。四者必须同步,否则代理制造矛盾比不加更糟。

## 关键代码锚点(调研已定位,省下次重查)

**Farbling 引擎**
- `third_party/blink/renderer/core/farbling/brave_session_cache.{h,cc}` — 核心。
  seed 派生 `MakePseudoRandomGenerator(FarbleKey)`;farbling level 枚举
  `brave_shields::mojom::FarblingLevel{OFF,BALANCED,MAXIMUM}`。改造点:让值取自 persona 而非 PRNG。
- 各面拦截(`chromium_src/.../` override):canvas=`base_rendering_context_2d.cc`(PerturbPixels);
  WebGL=`webgl_rendering_context_base.cc`;WebGPU=`modules/webgpu/gpu_adapter.cc`;
  audio=`webaudio/audio_buffer.cc` + `platform/brave_audio_farbling_helper.cc`;
  navigator=`navigator_base.cc` / `navigator_device_memory.cc` / `navigator_language.cc`;
  screen=`local_dom_window.cc`(FarbleInteger);字体=`css/local_font_face_source.cc` +
  `brave_font_whitelist.cc`。
- **未挂钩、需自己加**:maxTouchPoints、Gamepad、WebGL readPixels、**整个 L3 时区/geo**。
- 控制设置:`ContentSettingsType::BRAVE_FINGERPRINTING_V2`;`GetFarblingLevel`
  in `components/brave_shields/core/browser/brave_shields_utils.cc`。

**代理(Tor 已建好整套模板,克隆即可)**
- 注入缝:`chromium_src/chrome/browser/net/proxy_config_monitor.cc` —
  `if (profile->IsTor()) proxy_config_service_ = CreateProxyConfigServiceTor(profile)`。
- per-profile 服务模板:`net/proxy_resolution/proxy_config_service_tor.{h,cc}`。
- SOCKS5 user/pass 认证**已内置**:`chromium_src/net/socket/socks5_client_socket.cc`
  (`SOCKS5ClientSocketAuth`,RFC1929)。
- proxy 是 per-`Profile`/`NetworkContext`(`ProfileNetworkContextService` 持
  `ProxyConfigMonitor`)。
- auth 弹窗绕过:直接 `LoginHandler::SetAuth()` 或预填 `net/http/http_auth_cache.h`,无需 extension。

**设置页模板(仿 Tor 开关)**
- 后端 handler:`brave/browser/ui/webui/settings/brave_tor_handler.{h,cc}`,注册于
  `brave/browser/ui/webui/brave_settings_ui.cc`。
- 前端:`brave/browser/resources/settings/brave_tor_page/brave_tor_browser_proxy.ts`。

**L3 时区/geo 覆盖点(Chromium 原生,需自己接 per-profile)**
- 时区:`base/i18n/timezone.cc`(读 OS,不知代理);可 ICU 级 override 或 CDP `Emulation.setTimezoneOverride`。
- WebRTC 防漏:`--force-webrtc-ip-handling-policy=disable_non_proxied_udp`
  (pref `webrtc.ip_handling_policy`);唯一能彻底防公网 IP 泄漏的值(代价:UDP WebRTC 断)。
- Accept-Language:pref `intl.accept_languages` / `--lang`。
- geo:CDP `Emulation.setGeolocationOverride` + 关 OS 定位权限。
