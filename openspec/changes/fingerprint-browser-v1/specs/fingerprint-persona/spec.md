## ADDED Requirements

### Requirement: Persona 由真值池合成

系统 SHALL 从「真值池」（人工维护的、真实存在的候选值集合：真 WebGL
renderer 串、各 OS 真屏幕分辨率档、各 OS/locale 真字体集、真 UA + 版本组合等）中合成 persona，MUST
NOT 使用随机拼造、真机上不存在的值组合。

#### Scenario: 生成的 persona 每个字段都来自真值池

- **WHEN** 系统为一个 Profile 生成 persona
- **THEN** persona 的每个字段值都能在对应真值池中找到出处
- **AND** 不存在真值池之外凭空生成的指纹值

#### Scenario: 真值池为空或不足时拒绝生成

- **WHEN** 真值池缺少某必需维度（如某 OS 下无任何 renderer 候选）
- **THEN** 系统 SHALL 拒绝生成不完整的 persona 并报错
- **AND** MUST NOT 用占位/随机值填补缺口

### Requirement: 一致性引擎保证 persona 内部自洽

系统 SHALL 用一致性引擎组装 persona，确保各维度互相兼容（OS ↔ UA ↔ WebGL
renderer ↔ 字体集 ↔ 屏幕档 ↔ 时区 ↔ UA-CH platform），MUST
NOT 产出真机不可能出现的组合。

#### Scenario: OS 与 GPU 串兼容

- **WHEN** persona 的 OS 维度为 Windows
- **THEN** WebGL
  UNMASKED_RENDERER 串 SHALL 是 Windows 平台真实存在的显卡串（如 ANGLE/D3D11 形态）
- **AND** MUST NOT 出现 "Apple M#" 之类 macOS 专属串

#### Scenario: OS 与 UA-CH platform 一致

- **WHEN** persona 的 OS 维度为 Windows
- **THEN**
  navigator.userAgentData.platform 与 UA 字符串中的平台标识 SHALL 均为 Windows
- **AND** navigator.platform SHALL 为对应的 "Win32"/"Win64"

#### Scenario: 字体集与 OS/locale 匹配

- **WHEN** persona 的 OS 为 Windows、locale 为 en-US
- **THEN** persona 字体集 SHALL 是该 OS+locale 真实预装字体的子集
- **AND** MUST NOT 含该平台不存在的字体

### Requirement: Persona 为 per-Profile 且持久

系统 SHALL 将 persona 绑定到单个 Chromium
Profile 并持久化，使同一 Profile 在跨会话、重启、导航后输出恒定不变的指纹。

#### Scenario: 重启后 persona 不变

- **WHEN** 一个 Profile 已分配 persona，浏览器重启后重新打开该 Profile
- **THEN** 该 Profile 的所有指纹值 SHALL 与重启前完全一致

#### Scenario: 不同 Profile 得到不同 persona

- **WHEN** 创建两个不同的 Profile
- **THEN** 两者 SHALL 各自分配独立的 persona（受真值池容量限制，尽量不撞车）

#### Scenario: 同 Profile 跨站点指纹一致

- **WHEN** 同一 Profile 访问不同 eTLD+1 站点
- **THEN**
  指纹值 SHALL 保持一致（区别于 Brave 原生 farbling 的 per-eTLD+1 随机化）

#### Scenario: 同 Profile 内跨 Container/分区上下文一致

- **WHEN** 同一 Profile 内在不同 Container 标签页、或分区上下文（fenced frame /
  credentialless iframe）中读取指纹
- **THEN** 指纹值 SHALL 仍等于该 Profile 的 persona（token MUST NOT 因 Container
  id 或 storage-key nonce 分叉）
- **AND**
  这一 Profile 内一致性 SHALL 覆盖 canvas/WebGL/audio/Accept-Language 等所有 token 消费面

#### Scenario: 同 Profile 内跨 Worker 上下文一致

- **WHEN** 同一 Profile 内在 Dedicated/Shared/Service
  Worker（含 MV3 扩展 background service
  worker）中读取指纹（如 OffscreenCanvas、Worker AudioContext）
- **THEN**
  指纹值 SHALL 等于该 Profile 主文档的 persona（Worker 有独立 token 下发路径，须在根部
  `GetFarblingToken()` 收敛）
- **AND** MUST NOT 出现 Worker 内指纹与主文档不一致

### Requirement: Persona 覆盖 v1 所需全部字段

系统 MUST 为 Persona 提供 v1 所需的全部字段。字段至少覆盖 L1（navigator/screen/UA-CH/plugins/mimeTypes）、L2（canvas/audio 噪声种子、webgl/webgpu
metadata 串、字体集）；L3（时区/geo/语言）字段来自代理 IP 推导（见 proxy-geo-consistency），不在 Persona 内固化。

#### Scenario: persona 字段完整性校验

- **WHEN** 系统完成 persona 生成
- **THEN** persona
  SHALL 含 UA、UA-CH（platform/brands/version）、platform、hardwareConcurrency、deviceMemory、languages 基线、屏幕档（width/height/avail/colorDepth/DPR）、maxTouchPoints、plugins/mimeTypes、WebGL
  vendor/renderer、WebGPU adapter info、canvas/audio 噪声种子、字体集
- **AND** 缺任一字段则视为无效 persona

#### Scenario: schema 原位迁移不更换身份

- **WHEN** 已有 Profile 的合法旧 schema persona 被升级
- **THEN** 系统 SHALL 保留原 `persona_id` 和已有字段
- **AND** 仅补齐新 schema 所需字段

#### Scenario: 损坏 Persona 不静默换身份

- **WHEN** 已保存 persona 损坏、缺少身份字段或来自未知未来 schema
- **THEN** 系统 SHALL 回退 Brave 原生 farbling
- **AND** MUST NOT 静默生成新身份覆盖损坏数据

### Requirement: 新 Profile 自动分配 persona

系统 SHALL 在 Profile 创建时自动为其分配一个 persona，用户无需手动配置指纹（对应用户需求「我只需要改指纹」）。

#### Scenario: 新建 Profile 即带指纹

- **WHEN** 用户新建一个 Profile
- **THEN** 系统 SHALL 自动合成并绑定一个 persona
- **AND** 该 Profile 首次访问任意页面时即呈现该 persona 的指纹
