<script setup lang="ts">
defineProps<{
  open: boolean
  busy: boolean
}>()

defineEmits<{
  accept: []
  decline: []
}>()
</script>

<template>
  <Teleport to="body">
    <div v-if="open" class="consent-overlay">
      <section class="consent-dialog" role="dialog" aria-modal="true" aria-labelledby="consent-title">
        <header>
          <span>LIQUID GRAVITY PROBE</span>
          <h2 id="consent-title">使用与免责声明</h2>
          <p>插件会读取 Mods 并修改目标 Mod 的配置和运行时资源。请确认风险后进入编辑器。</p>
        </header>
        <div class="consent-body">
          <article><strong>非官方工具</strong><p>本插件与游戏开发商、发行商、平台及第三方 Mod 作者无隶属、授权或背书关系。</p></article>
          <article><strong>会修改 Mod 文件</strong><p>应用喷口、粒子效果或按键配置时会写入对应 Mod；插件不保留持久化原始 Mod 备份。</p></article>
          <article><strong>兼容与账号风险</strong><p>游戏更新、第三方 Mod、驱动或硬件差异可能导致效果异常、闪退或配置损坏。在线环境及账号风险由使用者自行评估。</p></article>
          <article><strong>第三方内容与责任</strong><p>不保证第三方模型、贴图、音频或成人内容的合法性、安全性、版权状态与持续兼容性。插件按现状提供。</p></article>
        </div>
        <footer>
          <button type="button" class="secondary" :disabled="busy" @click="$emit('decline')">不同意</button>
          <button type="button" class="primary" :disabled="busy" @click="$emit('accept')">
            {{ busy ? '正在保存…' : '同意并进入' }}
          </button>
        </footer>
      </section>
    </div>
  </Teleport>
</template>

<style scoped>
.consent-overlay { position:fixed; inset:0; z-index:2147483646; display:grid; place-items:center; padding:24px; box-sizing:border-box; background:rgba(2,5,7,.82); backdrop-filter:blur(12px); }
.consent-dialog { display:grid; grid-template-rows:auto minmax(0,1fr) auto; width:min(720px,calc(100vw - 48px)); max-height:calc(100vh - 48px); overflow:hidden; border:1px solid #34464d; border-radius:10px; background:#0b1215; color:#edf4f6; box-shadow:0 28px 80px rgba(0,0,0,.54); }
header { padding:22px 24px 18px; border-bottom:1px solid #24343a; background:linear-gradient(135deg,rgba(216,233,78,.07),transparent 42%); }
header span { color:#a9bb55; font:10px/1.4 ui-monospace,Consolas,monospace; }
header h2 { margin:5px 0 8px; font-size:22px; }
header p, article p { margin:0; color:#93a6ac; font-size:11px; line-height:1.7; }
.consent-body { display:grid; gap:12px; min-height:0; padding:18px 24px; overflow:auto; }
article { padding-left:12px; border-left:2px solid #677832; }
article strong { display:block; margin-bottom:3px; color:#e9f0cb; font-size:12px; }
footer { display:flex; justify-content:flex-end; gap:9px; padding:14px 24px 18px; border-top:1px solid #24343a; }
button { min-width:132px; min-height:36px; padding:8px 16px; border:1px solid #34464d; border-radius:6px; background:#111b1f; color:#dbe5e7; font:inherit; font-size:12px; cursor:pointer; }
button.primary { border-color:#d8e94e; background:#d8e94e; color:#11180b; font-weight:700; }
button:disabled { opacity:.6; cursor:wait; }
@media (max-width:560px) { .consent-overlay { padding:12px; } .consent-dialog { width:calc(100vw - 24px); } header,.consent-body,footer { padding-right:16px; padding-left:16px; } }
</style>
