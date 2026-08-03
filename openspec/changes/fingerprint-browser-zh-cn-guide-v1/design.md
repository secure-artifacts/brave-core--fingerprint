# 设计

## 使用指南

新增独立 WebUI `brave://fingerprint-guide/`。页面不读取 Profile 状态，不需要消息处理器，只提供五段简体中文步骤、代理状态图例和内部页面直达按钮。入口位于帮助菜单、代理设置页、代理工具栏弹窗、指纹检测页、诊断页和 `brave://crashes` 的自定义操作区。

页面固定 `lang="zh-CN"`、LTR，使用 Chromium WebUI 颜色变量，适配深浅主题和窄窗口。不内置截图，避免后续界面变更造成失效。

## 固定简体中文

自研 UI 的 GRIT 源字符串直接使用简体中文，因此即使浏览器以 `en-US` 启动也显示中文。技术值如 HTTP、HTTPS、WebRTC、UA-CH、WebGL、IP 和 IANA 时区保持原样。

指纹检测页的可见字符串全部由 `loadTimeData` 提供。诊断页、恢复提示和崩溃页自定义按钮继续复用 GRIT。诊断 ZIP 内的 README 改为简体中文，JSON schema、事件名和归档文件名保持不变。

## 代理消息

`FingerprintProxyState` 增加稳定的状态消息码和变化警告码；`ProxyVerificationResult` 增加错误码。UI 不再显示后端自由文本。现有字符串 pref 保留一个迁移周期：服务启动时把已知英文映射成代码，未知值映射为 `unknown`，之后只写代码。

工具栏与 Settings Handler 共用 C++ 消息码到 GRIT 字符串的映射。WebUI 同时获得 code 和简体中文 text，便于自动化断言。未知错误只显示通用中文提示和可选数字网络错误码。

国家名称按 ISO 国家码调用 ICU `zh-CN` 显示名；城市名是外部数据，保持 Provider 返回值。

## 兼容

- 现有 WebUI 方法名和核心字段不变，只增加 code/text 字段。
- 旧 Profile 无需用户操作即可迁移。
- Guest/Tor 原有限制不变。
- 指南不会在启动时自动打开，也不新增启动 pref。
