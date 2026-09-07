import DefaultTheme from 'vitepress/theme'

import AnnotatedScreenshot from './components/AnnotatedScreenshot.vue'
import './custom.css'

export default {
  extends: DefaultTheme,
  enhanceApp({app}) {
    app.component('AnnotatedScreenshot', AnnotatedScreenshot)
  },
}
