import { createApp, h } from 'vue'
import { createI18n } from 'vue-i18n'
import ElementPlus from 'element-plus'
import 'element-plus/dist/index.css'
import 'element-plus/theme-chalk/dark/css-vars.css'
import './theme.css'
import Settings from './Dlss5GraphicsSettings.vue'
import { capability, restoreActiveTask } from './bridge'

const locales = import.meta.glob('./locales/*.json', { eager: true, import: 'default' })
const messages = Object.fromEntries(Object.entries(locales).map(([path, value]) => [path.split('/').pop()!.replace('.json', ''), value]))
document.documentElement.classList.add('dark')
try {
  const context = await capability<{ gameName: string; targetExePath: string }>('game.context.read', {})
  const app = createApp({ render: () => h(Settings, context) })
  app.use(createI18n({ legacy: false, locale: navigator.language.startsWith('zh') ? (/TW|HK|Hant/i.test(navigator.language) ? 'zht' : 'zhs') : navigator.language.split('-')[0], fallbackLocale: 'en', messages: messages as any }))
  app.use(ElementPlus)
  app.mount('#app')
  restoreActiveTask().catch(error => console.error(error))
} catch (error) { document.getElementById('app')!.textContent = String(error) }
