import {defineConfig} from 'vitepress'

export default defineConfig({
  lang: 'zh-CN',
  title: '指纹浏览器使用手册',
  description: '面向新用户的指纹浏览器与用户配置文件代理使用说明',
  base: '/crx/brave-fingerprint/docs/',
  lastUpdated: false,
  markdown: {
    lineNumbers: false,
  },
  themeConfig: {
    logo: {
      light: '/logo.svg',
      dark: '/logo.svg',
      alt: '指纹浏览器',
    },
    nav: [
      {text: '快速开始', link: '/quick-start'},
      {text: '指纹功能', link: '/fingerprint'},
      {text: '代理功能', link: '/proxy'},
      {text: '排查问题', link: '/troubleshooting'},
    ],
    sidebar: [
      {
        text: '开始使用',
        items: [
          {text: '手册首页', link: '/'},
          {text: '五分钟快速开始', link: '/quick-start'},
        ],
      },
      {
        text: '核心功能',
        items: [
          {text: '认识浏览器指纹', link: '/fingerprint'},
          {text: '配置用户配置文件代理', link: '/proxy'},
        ],
      },
      {
        text: '帮助',
        items: [
          {text: '常见问题与排障', link: '/troubleshooting'},
        ],
      },
    ],
    outline: {
      level: [2, 3],
      label: '本页目录',
    },
    search: {
      provider: 'local',
      options: {
        locales: {
          root: {
            translations: {
              button: {
                buttonText: '搜索文档',
                buttonAriaLabel: '搜索文档',
              },
              modal: {
                noResultsText: '没有找到相关内容',
                resetButtonTitle: '清除搜索内容',
                footer: {
                  selectText: '选择',
                  navigateText: '切换',
                  closeText: '关闭',
                },
              },
            },
          },
        },
      },
    },
    docFooter: {
      prev: '上一页',
      next: '下一页',
    },
    darkModeSwitchLabel: '外观',
    lightModeSwitchTitle: '切换到浅色模式',
    darkModeSwitchTitle: '切换到深色模式',
    sidebarMenuLabel: '目录',
    returnToTopLabel: '返回顶部',
    skipToContentLabel: '跳到正文',
  },
})
