## ADDED Requirements

### Requirement: 通过代理出口查询 geo

系统 SHALL 通过候选代理请求 FreeIPAPI；请求超时、限流、网络失败或响应无效时 SHALL 请求
IPWHOIS.IO。两个 Provider 均须经统一适配层解析。v1 MUST NOT 依赖本地 GeoIP 数据库或其更新分发。

#### Scenario: 主 Provider 成功

- **WHEN** FreeIPAPI 在 6 秒内返回合法结果
- **THEN** 系统 SHALL 使用其出口 IP、国家、城市、IANA 时区和坐标
- **AND** SHALL 不请求备用 Provider

#### Scenario: 主 Provider 失败后切换备用

- **WHEN** FreeIPAPI 超时、返回 429、网络错误或无效 JSON
- **THEN** 系统 SHALL 经同一候选代理请求 IPWHOIS.IO
- **AND** 备用请求 SHALL 独立使用 6 秒超时

#### Scenario: 两个 Provider 均失败

- **WHEN** 两个 Provider 均无法返回合法结果
- **THEN** 首次配置 SHALL 禁止应用
- **AND** 已生效代理 SHALL 保留上次代理与派生指纹并进入 `stale`
- **AND** 页面 SHALL 显示结果已过期

#### Scenario: 返回结果严格校验

- **WHEN** Provider 返回结果
- **THEN** 出口 IP SHALL 为公网地址
- **AND** 国家码 SHALL 为合法 ISO 3166 两字母代码
- **AND** 时区 SHALL 为合法 IANA 时区
- **AND** 经纬度 SHALL 在合法范围
- **AND** 任一条件失败 SHALL 视为 Provider 失败

### Requirement: 确认后原子应用 L3

系统 SHALL 只在用户确认有效验证令牌后，原子应用代理、国家/国旗、IANA 时区、近似坐标、
按 ICU `und_<国家码>` 推导的主要语言和 Accept-Language，以及 WebRTC
`disable_non_proxied_udp`。

#### Scenario: 确认前不改变指纹

- **WHEN** 候选代理已验证但用户尚未确认
- **THEN** 当前 Profile 的代理、时区、语言、坐标和 WebRTC 策略 SHALL 保持不变

#### Scenario: 确认后同步更新

- **WHEN** 用户确认一个纽约出口验证结果
- **THEN** 代理、国家、时区、语言、坐标和 WebRTC SHALL 在同一次应用中更新
- **AND** 页面 MUST NOT 观测到代理已变但派生指纹仍旧的中间状态

### Requirement: 时区覆盖（L3）

系统 SHALL 使 `Intl.DateTimeFormat().resolvedOptions().timeZone` 与
`Date.getTimezoneOffset()` 反映代理推导时区，且 per-Profile 隔离。

#### Scenario: JS 时区等于代理时区

- **WHEN** 代理出口定位到 `America/New_York`
- **THEN** JS SHALL 返回 `America/New_York`
- **AND** `Date.getTimezoneOffset()` SHALL 与该时区一致

#### Scenario: 不同 Profile 时区互不影响

- **WHEN** Profile A 出口在纽约、Profile B 出口在伦敦
- **THEN** 两 Profile 的 JS 时区 SHALL 各自正确且互不干扰

### Requirement: 语言与地理坐标覆盖（L3）

系统 SHALL 使 Accept-Language 请求头、`navigator.languages`、`navigator.geolocation`
与代理国家和坐标一致。

#### Scenario: Accept-Language 匹配代理国家

- **WHEN** 代理出口定位到德国
- **THEN** Accept-Language 与 `navigator.languages` SHALL 反映 ICU 推导的德国主要 locale

#### Scenario: Strict 指纹保护不覆盖代理语言

- **WHEN** 站点处于 Strict 指纹保护且代理出口定位到德国
- **THEN** 系统 SHALL 协调二者
- **AND** MUST NOT 让固定英文语言静默覆盖代理语言

#### Scenario: geolocation 返回代理坐标

- **WHEN** 页面调用 `navigator.geolocation.getCurrentPosition`
- **THEN** 返回坐标 SHALL 为代理出口推导的近似经纬度
- **AND** MUST NOT 返回宿主机真实位置

#### Scenario: 禁用后恢复用户语言

- **WHEN** 用户此前手动设置语言，随后启用又禁用代理
- **THEN** 系统 SHALL 恢复用户原设置

### Requirement: 强制 WebRTC 防漏

系统 SHALL 在代理启用时强制 WebRTC IP 处理策略为 `disable_non_proxied_udp`。覆盖前 SHALL
保存用户原值；禁用代理时 SHALL 恢复原值。

#### Scenario: WebRTC 不泄漏真实 IP

- **WHEN** 代理启用且页面收集 ICE candidate
- **THEN** SHALL NOT 出现宿主机真实公网 IP 的 srflx candidate

#### Scenario: 禁用后恢复用户 WebRTC 设置

- **WHEN** 用户此前手动设置 WebRTC 策略，随后启用又禁用代理
- **THEN** 系统 SHALL 恢复用户原值
- **AND** MUST NOT 硬编码恢复 default

### Requirement: 定时复检与出口变化

系统 SHALL 每 15 分钟、浏览器启动、网络恢复和代理错误时复检出口；同一 Profile 多窗口 SHALL
合并并发复检。

#### Scenario: 同国家 IP 变化

- **WHEN** 复检发现出口 IP 变化但国家相同
- **THEN** 系统 SHALL 更新出口 IP
- **AND** SHALL 显示一次黄色变化提醒

#### Scenario: 国家变化

- **WHEN** 复检发现出口国家变化
- **THEN** 系统 SHALL 自动同步新时区、语言、坐标和国旗
- **AND** SHALL 持续显示变化提醒

#### Scenario: API 临时失败

- **WHEN** 已生效代理复检时两个 Provider 均临时失败
- **THEN** 系统 SHALL 保留代理和上次派生指纹
- **AND** SHALL 将国旗标记为 `stale`

#### Scenario: 代理连接或认证失败

- **WHEN** 复检或网页请求确认代理连接或认证失败
- **THEN** 系统 SHALL 进入 `error`
- **AND** 网页请求 SHALL 失败，MUST NOT DIRECT
