# 设计

主图使用白色 macOS 圆角方形底座、深灰指纹纹路、青蓝浏览器光圈和小面积金色状态点。图标在 16px 下仍以指纹外轮廓和中心光圈作为主要识别特征。

所有构建通道使用同一用户可见名称和主图，避免开发包与交付包出现品牌不一致。macOS 同时更新 app.icns 和 Assets.car；浏览器内部更新位图、SVG 及原生产品矢量图标。

Siso 无法可靠传递非 ASCII 链接路径。构建、可执行文件和 Framework 保留 `Fingerprint Browser`，Info.plist 通过 ASCII UTF-8 十六进制参数恢复并设置 `CFBundleDisplayName` 与 `CFBundleName` 为“指纹浏览器”。

QA 不再从进程名推断身份。可执行文件由 Info.plist 的 CFBundleExecutable 读取，进程清理只匹配唯一 /tmp/fingerprint-browser-* Profile。
