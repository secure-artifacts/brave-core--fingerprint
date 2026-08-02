## ADDED Requirements

### Requirement: 1 身份 = 1 Chromium Profile

系统 SHALL 以一个 Chromium
Profile 承载一个反检测身份，每个身份拥有独立的存储、NetworkContext、persona、代理配置（ADR-0004）。

#### Scenario: 身份要素齐备

- **WHEN** 创建一个身份
- **THEN** 系统 SHALL 为其建立独立 Profile，并绑定独立 persona 与（可选）代理
- **AND** 该身份的 cookie/localStorage/cache 存储 SHALL 与其他身份隔离

### Requirement: 多身份并发运行

系统 SHALL 支持多个 Profile 身份同时在线运行，各自维持独立 persona 与代理，MUST
NOT 因并发而串号。

#### Scenario: 并发身份互不串号

- **WHEN** 同时打开 Profile A 与 Profile B 并各自登录不同账号
- **THEN** 两者的指纹、代理、存储 SHALL 各自独立
- **AND** 一方的 cookie/登录态 SHALL NOT 泄漏到另一方

#### Scenario: 一方泄漏不关联另一方

- **WHEN** 两个并发身份运行在同一台宿主机
- **THEN** 其中一个身份的可观测指纹/IP SHALL
  NOT 暴露另一身份的存在或真实宿主标识

#### Scenario: 本地网络发现不绕过代理泄漏

- **WHEN** 一个启用代理的身份 Profile 运行、页面尝试本地网络设备发现（如 Media
  Router/Cast、mDNS）
- **THEN** 该发现 SHALL NOT 绕过代理暴露真实本地网络/设备图谱信息
- **AND** 系统 SHALL 审计 Tor 已有的同类保护（如 Tor 窗口禁用 Media
  Router）是否需扩展到 per-Profile 代理身份

### Requirement: persona ↔ proxy ↔ Profile 绑定生命周期

系统 SHALL 管理 persona 与代理到 Profile 的绑定生命周期：创建时分配，销毁时清理，MUST
NOT 残留孤立绑定。

#### Scenario: 创建时绑定

- **WHEN** 新建一个身份 Profile
- **THEN** 系统 SHALL 分配 persona（自动）并允许配置代理
- **AND** 绑定关系持久化到该 Profile

#### Scenario: 销毁时清理

- **WHEN** 删除一个身份 Profile
- **THEN** 系统 SHALL 清理其 persona 绑定、代理配置与隔离存储
- **AND** 不残留可被复用/误关联的孤立数据

### Requirement: 未配置代理时的降级行为

系统 SHALL 明确定义「有 persona 但未启用代理」的行为：指纹仍走 persona，但 L3（时区/geo/语言）在无代理时回退到宿主机或 persona 默认，并向用户提示此时存在 IP 与指纹地理不一致风险。

#### Scenario: 仅指纹无代理

- **WHEN** 一个 Profile 有 persona 但未启用代理
- **THEN** L1/L2 指纹 SHALL 仍呈现 persona 值
- **AND** 系统 SHALL 提示「未启用代理，出口 IP 为真实网络，地理一致性无法保证」
