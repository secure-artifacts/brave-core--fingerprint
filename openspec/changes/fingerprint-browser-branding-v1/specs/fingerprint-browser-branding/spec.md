# 指纹浏览器品牌规范

## ADDED Requirements

### Requirement: 简体中文产品名称

系统 MUST 在 macOS Finder、Dock、菜单和浏览器产品界面中显示“指纹浏览器”。内部应用包、可执行文件和 Framework 可以使用 ASCII 名称 `Fingerprint Browser`，但不得影响用户可见名称。

#### Scenario: 用户查看 macOS 应用身份

- **WHEN** 用户启动当前 macOS 产物并查看 Finder、Dock 或应用菜单
- **THEN** 产品名称显示为“指纹浏览器”
- **AND** 应用仍可使用内部 ASCII 可执行文件和 Framework 正常启动

### Requirement: 自定义产品图标

系统 MUST 在 Dock、Finder、关于页面、状态托盘和产品级提示中使用自定义指纹光圈 Logo，不得继续显示 Brave 狮子产品 Logo。

#### Scenario: 用户查看产品级图标

- **WHEN** 用户查看应用图标或浏览器内产品标识
- **THEN** 显示自定义指纹光圈 Logo
- **AND** 不显示 Brave 狮子产品 Logo

### Requirement: QA 品牌兼容

QA MUST 从当前应用包读取可执行文件名称，只清理指定临时 Profile 对应进程，并同时识别新旧开发版名称生成的本机崩溃报告。

#### Scenario: QA 验证当前品牌产物

- **WHEN** QA 使用唯一 `/tmp/fingerprint-browser-*` Profile 启动当前应用包
- **THEN** QA 从 Info.plist 读取实际可执行文件
- **AND** 只清理该 Profile 对应进程
- **AND** 对当前、内部 ASCII 和旧开发版名称的崩溃报告执行检查
