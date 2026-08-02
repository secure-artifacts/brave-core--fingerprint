## ADDED Requirements

### Requirement: 复用 Brave farbling 拦截点、替换为 persona 值

系统 SHALL 复用 Brave farbling 的现有拦截点（`brave_session_cache` 及各
`chromium_src/.../`
override），把「per-session 随机加噪」逻辑替换为「返回当前 Profile
persona 的持久值」；当 persona 生效时，farbling 的随机化 MUST
NOT 再作用于已被 persona 接管的指纹面。

#### Scenario: 指纹面取 persona 值而非随机噪声

- **WHEN**
  一个带 persona 的 Profile 读取某被 farbling 覆盖的指纹面（如 navigator.hardwareConcurrency）
- **THEN** 返回值 SHALL 等于 persona 对应字段
- **AND** 同一 Profile 多次读取该值 SHALL 恒定（非 per-session 随机）

#### Scenario: 种子替换覆盖真正的 token 来源

- **WHEN** 实现指纹替换
- **THEN** persona 派生 SHALL 下沉到 farbling token 的真正来源（browser 侧
  `GetFarblingToken()`，per-URL 键控、持久化于 website-setting），使 token 变为 per-Profile 恒定、site-invariant
- **AND** SHALL 覆盖 token 的**实际下发路径**（常规导航经
  `SendShieldsSettings()` push → `SetShieldsSettings` →
  `BraveSessionCache::default_shields_settings_`），而非仅未见调用方的
  `content_settings_manager_delegate` 接口
- **AND** SHALL 覆盖 `farbling_token`
  的所有直接消费点（canvas/audio/WebGL-extension 直接读取，不经
  `MakePseudoRandomGenerator`）
- **AND** SHALL 复用既有 override 文件，不为已覆盖的面另建平行拦截

### Requirement: 尊重逐站点、逐面指纹保护开关

系统在施加 persona 前 SHALL **按各面自己的 `ContentSettingsType`**
检查该面有效 farbling 级别（`GetBraveFarblingLevel(BRAVE_WEBCOMPAT_CANVAS/_AUDIO/_WEBGL/_FONT/...)`），而非仅站点级
`BRAVE_FINGERPRINTING_V2`；当某面被站点级 OFF 或**逐面 webcompat 例外**（可在站点级仍 BALANCED 时把单面强制 OFF）置为 OFF 时，该面 MUST 保留真实值、MUST
NOT 施加 persona。

#### Scenario: 站点关闭指纹保护时不施加 persona

- **WHEN** 某站点的 Shields 指纹保护被设为 OFF
- **THEN** 该站点读取指纹面（canvas/navigator 等）SHALL 得到真实值
- **AND** persona 的自动分配 MUST NOT 覆盖这一逐站点开关

#### Scenario: 逐面 webcompat 例外被尊重

- **WHEN**
  站点级指纹保护为 BALANCED，但某单面（如 canvas）存在 webcompat 例外将其强制为 OFF
- **THEN** 该面 SHALL 保留真实值，其余面仍施加 persona
- **AND** persona 接管 MUST NOT 无视逐面例外一律覆盖

### Requirement: L1 navigator/UA-CH 取自 persona

系统 SHALL 使 navigator 及 User-Agent Client
Hints 家族的取值来自 persona：userAgent、userAgentData(platform/brands/fullVersionList)、platform、hardwareConcurrency、deviceMemory、languages(基线)、plugins/mimeTypes、vendor。

#### Scenario: navigator 值与 persona 一致

- **WHEN** 页面读取 navigator.hardwareConcurrency / deviceMemory / platform /
  userAgent
- **THEN** 各值 SHALL 分别等于 persona 的对应字段

#### Scenario: UA 与 UA-CH 互相一致

- **WHEN**
  页面同时读取 navigator.userAgent 与 navigator.userAgentData.getHighEntropyValues()
- **THEN** 两者的平台/版本信息 SHALL 一致，无矛盾

#### Scenario: 网络请求 User-Agent 头等于 persona

- **WHEN**
  浏览器就该 Profile 发出任意网络请求（导航/子资源/XHR/fetch/WebSocket/worker）
- **THEN** 实际发送的 `User-Agent:`
  HTTP 头（及 UA-CH 头）SHALL 反映 persona，而非真实宿主 UA
- **AND**
  wire 头 SHALL 与 JS 侧 navigator.userAgent 一致（二者是独立代码路径，须都接 persona）

#### Scenario: profile-无关的系统流量不被 persona 污染

- **WHEN**
  存在活跃 persona 的 Profile，浏览器发出不关联任何 Profile 的系统级请求（System
  NetworkContext）
- **THEN** 该系统请求的 `User-Agent:` SHALL 仍为真实未 spoof UA
- **AND** persona UA 覆盖 MUST 仅作用于 per-Profile NetworkContext，MUST
  NOT 改动被系统上下文共享的 UA 源函数

### Requirement: L1 screen/window 取自 persona

系统 SHALL 使 screen（width/height/availWidth/availHeight/colorDepth/pixelDepth）、window.devicePixelRatio 取自 persona，且各值互相合理（avail
≤ 物理，DPR 与分辨率档匹配）。

#### Scenario: 屏幕维度来自 persona

- **WHEN** 页面读取 screen.width/height 与 window.devicePixelRatio
- **THEN** 各值 SHALL 等于 persona 屏幕档
- **AND** availWidth/availHeight SHALL ≤ 对应物理宽高

### Requirement: 补齐 Brave 未挂钩的指纹面

系统 SHALL 为 Brave
farbling 未覆盖或覆盖不足的指纹面新增/接入 persona：navigator.maxTouchPoints、Gamepad、WebGL
readPixels、Local Font Access
API（`navigator.fonts.query()`）、鼠标/指针/触摸事件 screenX/screenY（现丢弃真实值、与 window 位置不自洽）、media
devices 枚举（真实 label/count 未过滤）、speech
synthesis 语音列表（BALANCED 仍透出真实已装语音），使其亦反映 persona 或至少与 persona 自洽。

#### Scenario: maxTouchPoints 与设备类型一致

- **WHEN** persona 为桌面设备，页面读取 navigator.maxTouchPoints
- **THEN** 返回值 SHALL 为 0（不与桌面 UA 矛盾）

#### Scenario: readPixels 与 canvas 读回一致

- **WHEN** 页面通过 WebGL readPixels 读取像素
- **THEN** 结果 SHALL 与 persona 的 L2 噪声处理一致，不绕过指纹处理

#### Scenario: Local Font Access 不泄漏宿主字体

- **WHEN** 页面调用 `navigator.fonts.query()` 枚举本地字体
- **THEN** 返回结果 SHALL 限于 persona 字体集（或被屏蔽）
- **AND** MUST NOT 泄漏宿主机真实安装的、不属于 persona 的字体

#### Scenario: 媒体设备保持真实可用

- **WHEN** 页面枚举摄像头或麦克风
- **THEN** 系统 SHALL 只映射实际存在的设备类型，每类最多暴露一个泛化入口
- **AND** MUST NOT 虚构不存在的摄像头或麦克风
- **AND** 页面把泛化 `deviceId` 传给 `getUserMedia()`
  时 SHALL 反向映射并打开对应真实设备

#### Scenario: Gamepad 保持交互能力

- **WHEN** 用户连接并操作真实 Gamepad
- **THEN** 页面 SHALL 继续收到连接事件、按键和摇杆状态
- **AND** 只将硬件名称和稳定标识替换为泛化值

### Requirement: L2 canvas/audio 用 per-Profile 持久噪声

系统 SHALL 对 canvas（toDataURL/getImageData/toBlob）与 AudioContext 施加确定性噪声，噪声种子来自 persona（同 Profile 恒定），使渲染 hash 稳定且 per-Profile 唯一；MUST
NOT 用 per-session 随机种子。

#### Scenario: canvas hash 稳定且唯一

- **WHEN** 同一 Profile 两次生成相同内容的 canvas 指纹
- **THEN** 两次 hash SHALL 相同
- **AND** 不同 persona 的 Profile 对相同内容 SHALL 产出不同 hash

#### Scenario: audio 指纹稳定

- **WHEN** 同一 Profile 两次计算 AudioContext 指纹
- **THEN** 两次结果 SHALL 一致

### Requirement: L2 WebGL/WebGPU metadata 串取自 persona 真值

系统 SHALL 使 WebGL UNMASKED_VENDOR/RENDERER 及 WebGPU
GPUAdapterInfo 返回 persona 真值池中的真实显卡串，MUST
NOT 返回随机字符串或清空。

#### Scenario: WebGL renderer 为真值池中的真实串

- **WHEN** 页面读取 WebGL UNMASKED_RENDERER_WEBGL
- **THEN** 返回值 SHALL 是 persona 指定的、真值池中真实存在的显卡串
- **AND** SHALL 与 persona OS 平台兼容

### Requirement: 仅在 Chrome 系内伪装、L4 保持原生

系统 SHALL 将伪装限制在 Chrome 浏览器家族内（UA 始终为 Chrome，仅改版本/OS 等 Chrome 内维度），MUST
NOT 伪装成非 Chrome 引擎；网络层 L4（TLS/JA3/JA4、HTTP2）保持原版 Chromium，不做改动，以确保 L4 自动与 Chrome
UA 一致。

#### Scenario: UA 始终 Chrome 家族

- **WHEN** 检视任一 persona 的 UA
- **THEN** 其浏览器标识 SHALL 为 Chrome
- **AND** MUST NOT 出现 Safari/Firefox 等非 Chrome 引擎标识

#### Scenario: L4 未被改动

- **WHEN** 对比本浏览器与原版 Chromium 的 TLS ClientHello
- **THEN** TLS 指纹 SHALL 与原版 Chromium 一致（未被本项目修改）

### Requirement: 全指纹面反映同一 persona（无矛盾）

系统 SHALL 保证一个 Profile 的所有指纹面（L1+L2+L3）反映同一个 persona，MUST
NOT 出现跨面矛盾（如 UA=Windows 而 renderer=Apple、桌面 UA 而 maxTouchPoints>0）。

#### Scenario: 跨面一致性校验

- **WHEN** 用指纹检测页（如 CreepJS/FingerprintJS 类）扫描一个 Profile
- **THEN** 各面报告的 OS/设备/浏览器 SHALL 互相一致
- **AND** 不触发「lied/mismatch」类判定
