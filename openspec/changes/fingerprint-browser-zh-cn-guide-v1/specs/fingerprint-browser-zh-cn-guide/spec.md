# 指纹浏览器简体中文指南规范

## ADDED Requirements

### Requirement: 可长期访问的中文指南

系统 SHALL 提供静态 `brave://fingerprint-guide/`，覆盖 Profile、代理、指纹检测、代理状态和诊断导出，并提供所有相关内部页面的直达按钮。

#### Scenario: 从帮助菜单进入

- **WHEN** 用户在任意普通窗口打开帮助菜单并选择“指纹浏览器使用指南”
- **THEN** 系统打开 `brave://fingerprint-guide/`

#### Scenario: 页面互链

- **WHEN** 用户位于代理设置、代理弹窗、指纹检测、诊断或崩溃页
- **THEN** 页面提供“使用指南”入口并打开同一指南页面

#### Scenario: 不自动打开

- **WHEN** 浏览器或新 Profile 首次启动
- **THEN** 系统不自动打开指南页面

### Requirement: 自研 UI 固定简体中文

代理、指纹检测、诊断导出、恢复提示和自定义崩溃操作 SHALL 在任意 Brave locale 下显示简体中文。

#### Scenario: 英文浏览器环境

- **WHEN** 浏览器使用 `--lang=en-US` 启动
- **THEN** 所有自研功能标题、按钮、状态、校验和错误仍为简体中文

#### Scenario: 技术值保持原样

- **WHEN** UI 显示协议、IP、IANA 时区或 Web 标准名
- **THEN** 技术值保持标准格式，周围标签和解释为简体中文

### Requirement: 代理 UI 不显示后端英文

代理服务 SHALL 通过稳定代码表达状态、警告和错误；UI SHALL 使用固定中文资源显示。

#### Scenario: 旧 Profile 迁移

- **WHEN** Profile 保存了旧英文状态消息
- **THEN** 系统映射为稳定代码并显示中文，未知旧值显示中文通用提示

#### Scenario: 未知运行错误

- **WHEN** 代理返回未识别错误
- **THEN** UI 显示中文通用错误及可选数字网络错误码，不显示英文原始消息

### Requirement: 中文国家名称

系统 SHALL 根据代理 Geo 数据中的 ISO 国家码生成固定简体中文国家名称。

#### Scenario: 代理验证成功

- **WHEN** Geo Provider 返回有效 ISO 国家码
- **THEN** UI 使用 ICU `zh-CN` 国家名称，并保留城市、IP、时区等原始技术数据

### Requirement: 自动化防回归

测试 SHALL 在 `en-US` 环境验证中文文案、入口导航、布局和残留英文审计。

#### Scenario: 最新 QA 产物

- **WHEN** 执行交付验收
- **THEN** 使用当前源码生成的最新动态库和资源包，完成深浅主题、三种尺寸、Smoke、崩溃扫描和截图分析
