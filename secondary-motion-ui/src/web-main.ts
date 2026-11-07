import { createApp } from 'vue'
import ElementPlus from 'element-plus'
import 'element-plus/dist/index.css'
import 'element-plus/theme-chalk/dark/css-vars.css'
import './web.css'
import SecondaryMotionSettings from './SecondaryMotionSettings.vue'

const params = new URLSearchParams(window.location.hash.replace(/^#/, ''))
const game = params.get('game') || 'EFMI'

createApp(SecondaryMotionSettings, { gameName: game })
  .use(ElementPlus)
  .mount('#app')
