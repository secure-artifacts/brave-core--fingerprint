# 五分钟快速开始

第一次使用时，按顺序完成下面 5 步。

## 操作步骤

<div class="step-list">
  <div>
    <h3>创建独立用户配置文件</h3>
    <p>点击右上角用户图标，选择“添加用户配置文件”。每个账号单独使用一个。</p>
  </div>
  <div>
    <h3>打开代理设置</h3>
    <p>点击工具栏代理图标，再点击“配置”。找不到入口时，复制下方地址。</p>
  </div>
  <div>
    <h3>填写并验证代理</h3>
    <p>按代理服务商的信息填写类型、地址、端口和账号密码，再点“验证代理”。</p>
  </div>
  <div>
    <h3>确认并应用</h3>
    <p>国家、时区和语言正确后，点击“确认并应用”。</p>
  </div>
  <div>
    <h3>检查浏览器指纹</h3>
    <p>先打开普通网页，再打开指纹检测页。所有结果应显示“匹配”。</p>
  </div>
</div>

找不到代理入口时，把这行地址复制到地址栏：

```text
brave://settings/fingerprintProfileProxy
```

## 找到代理设置

<AnnotatedScreenshot
  src="./screenshots/proxy-settings.png"
  alt="用户配置文件代理设置页面"
  caption="代理尚未配置时的设置页面"
  :callouts="[
    {x: 72, y: 28, title: '选择类型', description: '必须与代理服务商提供的类型一致。'},
    {x: 64, y: 47, title: '填写信息', description: '账号密码没有时可以留空。'},
    {x: 78, y: 65, title: '开始验证', description: '验证成功后还要确认，代理才会生效。'},
  ]"
/>

## 怎样算配置成功

应用成功后，页面会显示出口地区，工具栏会出现国旗和绿色状态点。

<AnnotatedScreenshot
  src="./screenshots/proxy-active.png"
  alt="真实代理成功应用后的状态页面"
  caption="代理经过真实连接验证并应用后的状态，敏感连接信息已经遮盖"
  :callouts="[
    {x: 45, y: 25, title: '出口位置', description: '确认国家和城市符合预期。'},
    {x: 65, y: 35, title: '联动信息', description: '时区和语言会随代理自动设置。'},
    {x: 76, y: 46, title: '立即复检', description: '状态异常时点这里重新检查。'},
  ]"
/>

::: warning 登录前再看一眼工具栏
绿色可以继续；黄色先复检；红色先停用代理并检查配置。
:::
