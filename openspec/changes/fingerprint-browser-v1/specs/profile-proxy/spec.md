## ADDED Requirements

### Requirement: per-Profile 代理配置

系统 SHALL 支持为普通 Profile 配置独立上游代理，作用域为该 Profile 的 NetworkContext，MUST
NOT 影响其他 Profile 或全局。普通隐身窗口 SHALL 继承原 Profile 的代理与状态；Tor 和 Guest
SHALL 保持原网络行为，不被本功能接管。

#### Scenario: 代理仅作用于本 Profile

- **WHEN** Profile A 已确认并启用代理、Profile B 未配置
- **THEN** Profile A 的流量 SHALL 走代理
- **AND** Profile B 的流量 SHALL 直连、不受影响

#### Scenario: 代理配置持久化

- **WHEN** 为一个 Profile 确认代理并重启浏览器
- **THEN** 重启后该 Profile SHALL 保留代理配置和上次验证状态
- **AND** 系统 SHALL 自动复检出口

#### Scenario: 隐身与特殊 Profile

- **WHEN** 普通 Profile 打开隐身窗口
- **THEN** 隐身窗口 SHALL 继承原 Profile 的代理与状态
- **AND WHEN** 窗口属于 Tor 或 Guest
- **THEN** 本功能 SHALL 不接管其代理

#### Scenario: 不破坏 Tor Profile 代理

- **WHEN** `proxy_config_monitor` 支持通用 per-Profile 代理
- **THEN** `profile->IsTor()` 分支 SHALL 严格优先于通用代理判断
- **AND** Tor Profile SHALL 继续使用专用 `ProxyConfigServiceTor`

#### Scenario: 与扩展/企业策略代理冲突

- **WHEN** 某 Profile 的代理已被企业策略或 `chrome.proxy` 扩展管控
- **THEN** 本功能 SHALL 进入 `conflict` 状态并显示原因
- **AND** SHALL 让标准代理路径继续处理既有配置
- **AND** MUST NOT 静默覆盖

### Requirement: 支持 HTTP、HTTPS 与 SOCKS5 认证代理

系统 SHALL 支持 HTTP、HTTPS 与 SOCKS5 代理，均支持用户名/密码认证。认证 MUST
NOT 弹出交互式登录框。SOCKS5 认证使用 RFC1929；HTTP/HTTPS 自动处理代理认证挑战。

#### Scenario: SOCKS5 带认证连接

- **WHEN** 配置需要账号密码的 SOCKS5 代理
- **THEN** 浏览器 SHALL 用 RFC1929 完成认证并建立连接
- **AND** 全程无认证弹窗

#### Scenario: HTTP 或 HTTPS 带认证连接

- **WHEN** 配置需要账号密码的 HTTP 或 HTTPS 代理
- **THEN** 浏览器 SHALL 自动提供配置凭证
- **AND** 全程无认证弹窗

#### Scenario: 代理失败不直连

- **WHEN** 已生效代理连接失败、认证失败或保存凭证无法解密
- **THEN** 网页请求 SHALL 失败并显示 `error`
- **AND** MUST NOT 回退 DIRECT
- **AND** MUST NOT 用旧国旗误导用户

### Requirement: 验证后确认状态机

系统 SHALL 使用
`unconfigured/verifying/awaiting_confirmation/active/stale/error/conflict`
状态。用户必须先验证草稿，再使用绑定该草稿的验证令牌确认应用。验证失败 MUST
NOT 改变当前已生效代理。

#### Scenario: 候选代理隔离验证

- **WHEN** 用户提交协议、host、端口、用户名和密码草稿
- **THEN** 系统 SHALL 创建独立、仅内存的验证 NetworkContext
- **AND** SHALL 禁用 Cookie、缓存、重定向和页面凭证
- **AND** SHALL 只使用候选代理，MUST NOT DIRECT 回退

#### Scenario: 验证成功后等待确认

- **WHEN** 候选代理成功返回合法出口和位置
- **THEN** 系统 SHALL 返回结果和一次性 `verificationId`
- **AND** 状态 SHALL 为 `awaiting_confirmation`
- **AND** 当前代理 MUST NOT 在用户确认前改变

#### Scenario: 验证期间草稿竞态

- **WHEN** 验证请求尚未完成
- **THEN** 页面 SHALL 锁定代理输入
- **AND WHEN** 草稿版本已经变化但旧请求随后返回
- **THEN** 系统 SHALL 丢弃旧结果且 MUST NOT 显示或应用

#### Scenario: 草稿变化或令牌过期

- **WHEN** 用户修改任一草稿字段，或验证令牌超过 5 分钟
- **THEN** 旧验证结果 SHALL 不可应用
- **AND** 用户 SHALL 重新验证

#### Scenario: 令牌只能使用一次

- **WHEN** 同一 `verificationId` 已成功应用
- **THEN** 后续重复确认 SHALL 被拒绝

### Requirement: 代理凭证安全

系统 SHALL 使用 Chromium `OSCryptAsync` 加密持久化密码。WebUI SHALL 只获得
`hasSavedPassword`，MUST NOT 读取保存的明文密码。日志、崩溃报告和 QA 报告 MUST
NOT 记录用户名或密码。

#### Scenario: 留空密码继续使用已保存密码

- **WHEN** 用户编辑已保存代理并将密码字段留空
- **THEN** 系统 SHALL 在内部使用已加密保存的密码
- **AND** WebUI SHALL 不收到该密码

#### Scenario: 迁移旧明文密码

- **WHEN** Profile 存在旧明文密码 pref 且加密器可用
- **THEN** 系统 SHALL 加密保存密码
- **AND** 成功后 SHALL 清除旧明文 pref

#### Scenario: Keychain 解密失败

- **WHEN** 已保存密码无法通过 Keychain/OSCryptAsync 解密
- **THEN** 系统 SHALL 保持代理阻断状态并显示错误
- **AND** MUST NOT 临时直连

### Requirement: 三阶段设置页

系统 SHALL 在 `brave://settings/fingerprintProfileProxy`
提供“输入代理 → 验证代理 → 查看结果并确认应用”三阶段独立页面。隐私设置页 SHALL 提供可见入口，工具栏状态弹窗的配置操作 SHALL 直接进入该页面。表单包含协议、host、端口、用户名和密码；结果包含国旗、出口 IP、国家/城市、IANA 时区、坐标、语言和 WebRTC 状态。

#### Scenario: 用户验证并确认代理

- **WHEN** 用户填写合法代理并点击验证
- **THEN** 页面 SHALL 显示验证结果但不立即改变当前代理
- **AND WHEN** 用户点击“确认并应用”
- **THEN** 系统 SHALL 原子应用代理和派生指纹配置
- **AND**
  配置、凭证、Geo、语言、时区、定位或 WebRTC 任一步准备失败时 SHALL 完整回滚
- **AND** 页面 MUST NOT 观察到半配置状态

#### Scenario: 从设置页和工具栏进入代理页面

- **WHEN** 用户点击隐私设置页的 Profile proxy 入口
- **THEN** 浏览器 SHALL 导航到 `brave://settings/fingerprintProfileProxy`
- **AND WHEN** 用户点击工具栏代理按钮并选择配置
- **THEN** 浏览器 SHALL 直接打开同一代理设置页面

#### Scenario: 字段校验失败

- **WHEN** 协议、host 或端口无效
- **THEN** 页面 SHALL 给出字段级错误
- **AND** MUST NOT 发起验证或修改当前代理

#### Scenario: 禁用代理

- **WHEN** 用户点击禁用代理
- **THEN** 当前 Profile SHALL 恢复直连
- **AND** 与代理绑定的派生覆盖 SHALL 恢复原用户设置

### Requirement: 浏览器工具栏代理状态

系统 SHALL 在 VPN 后、Profile/App
Menu 前提供固定代理按钮，并以语义状态持续显示当前 Profile 代理健康。国旗 SHALL 来自固定版本本地资源，MUST
NOT 加载 API 返回的远程图片。

#### Scenario: 未配置和已生效状态

- **WHEN** Profile 未配置代理
- **THEN** 按钮 SHALL 显示灰色代理图标和黄色警告
- **AND WHEN** 代理已生效且国家已知
- **THEN** 按钮 SHALL 持续显示出口国家国旗

#### Scenario: 过期、错误和冲突状态

- **WHEN** geo 结果已过期
- **THEN** 国旗 SHALL 增加黄色徽标
- **AND WHEN** 连接或认证失败
- **THEN** 按钮 SHALL 显示语义红色错误图标且隐藏旧国旗
- **AND WHEN** 存在策略或扩展冲突
- **THEN** 按钮 SHALL 显示受控状态与原因
- **AND** SHALL 立即恢复原语言、WebRTC、时区和定位并隐藏旧国旗
- **AND WHEN** 冲突解除
- **THEN** 系统 SHALL 重新验证后才恢复 active

#### Scenario: 状态弹窗

- **WHEN** 用户点击工具栏代理按钮
- **THEN** 弹窗 SHALL 显示当前出口、最后验证时间、立即复检、配置和禁用操作
- **AND** 完整编辑 SHALL 进入设置页
