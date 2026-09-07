# 配置用户配置文件代理

代理只影响当前用户配置文件，每个用户配置文件可以使用不同代理。

## 准备代理信息

向代理服务商取得以下内容：

| 内容 | 示例格式 | 是否必填 |
| --- | --- | --- |
| 代理类型 | `HTTP`、`HTTPS` 或 `SOCKS5` | 是 |
| 代理地址 | 域名或 IP 地址 | 是 |
| 端口 | `1` 到 `65535` | 是 |
| 用户名 | 服务商提供的账号 | 按服务商要求 |
| 密码 | 服务商提供的密码 | 按服务商要求 |

::: warning 不要把代理信息发给陌生人
截图或求助前，请遮盖代理地址、用户名、密码和完整出口 IP。
:::

## 填写并验证

1. 在地址栏输入 `brave://settings/fingerprintProfileProxy`。
2. 选择代理类型。
3. 填写地址、端口；需要认证时再填用户名和密码。
4. 点击“验证代理”。

<AnnotatedScreenshot
  src="/screenshots/proxy-settings.png"
  alt="代理类型和连接信息填写区域"
  caption="先填写代理服务商提供的信息，再进行验证"
  :callouts="[
    {x: 72, y: 28, title: '代理类型', description: '必须与服务商提供的一致。'},
    {x: 54, y: 37, title: '代理地址', description: '只填域名或 IP，不要带 http://。'},
    {x: 77, y: 37, title: '端口', description: '只填数字。'},
    {x: 59, y: 52, title: '账号密码', description: '代理需要认证时再填写。'},
    {x: 78, y: 65, title: '验证代理', description: '先验证，成功后再确认应用。'},
  ]"
/>

## 核对验证结果

验证成功后，核对国家和城市是否符合购买地区，再点击“确认并应用”。

<AnnotatedScreenshot
  src="/screenshots/proxy-verified.png"
  alt="真实代理验证成功后的确认页面"
  caption="真实代理验证结果，账号、密码和网络地址的敏感部分已经遮盖"
  :callouts="[
    {x: 49, y: 68, title: '出口位置', description: '核对国家、城市和出口 IP。'},
    {x: 59, y: 80, title: '环境信息', description: '时区、语言和 WebRTC 会一起应用。'},
    {x: 76, y: 91, title: '确认并应用', description: '核对无误后点击。'},
  ]"
/>

## 代理生效后

<AnnotatedScreenshot
  src="/screenshots/proxy-active.png"
  alt="代理处于正常生效状态"
  caption="代理生效后可查看出口和最后验证时间，也可以立即复检或禁用"
  :callouts="[
    {x: 50, y: 24, title: '当前出口', description: '查看国家、城市和出口 IP。'},
    {x: 69, y: 41, title: '验证时间', description: '时间过久时可立即复检。'},
    {x: 76, y: 46, title: '管理代理', description: '可以复检或禁用代理。'},
  ]"
/>

应用后，浏览器会自动：

- 让当前用户配置文件通过代理上网。
- 按出口设置国家、时区、语言和地理位置。
- 阻止 WebRTC 绕过代理，并定期复检代理。

## 工具栏状态怎么看

<div class="status-grid">
  <div class="status-card">
    <strong><span class="status-dot status-dot--green"></span>绿色</strong>
    <p>正常，可以使用。</p>
  </div>
  <div class="status-card">
    <strong><span class="status-dot status-dot--yellow"></span>黄色</strong>
    <p>需要确认，先点“立即复检”。</p>
  </div>
  <div class="status-card">
    <strong><span class="status-dot status-dot--red"></span>红色</strong>
    <p>停止使用，并检查配置。</p>
  </div>
</div>

国旗代表出口国家，旁边的小圆点代表代理状态。

<AnnotatedScreenshot
  src="/screenshots/proxy-toolbar.png"
  alt="浏览器工具栏中的代理国旗和状态弹窗"
  caption="工具栏入口可以快速查看代理状态、复检或进入完整设置"
  :callouts="[
    {x: 89, y: 16, title: '国旗与状态点', description: '国旗看国家，圆点看状态。'},
    {x: 73, y: 45, title: '状态详情', description: '查看当前出口和提示。'},
    {x: 72, y: 77, title: '快捷操作', description: '复检、禁用或打开设置。'},
  ]"
/>

## 修改或停用代理

- **更换代理**：修改后重新验证，再确认应用。
- **立即复检**：状态变黄或怀疑网络异常时使用。
- **禁用代理**：让当前用户配置文件恢复原来的网络设置。
- **密码已保存**：只改其他字段时，密码框可以留空。
