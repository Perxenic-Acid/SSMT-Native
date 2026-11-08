import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import { fileURLToPath } from 'node:url'
export default defineConfig({
  root: fileURLToPath(new URL('.', import.meta.url)), plugins: [vue()],
  build: { target: 'es2022', emptyOutDir: false, cssCodeSplit: false,
    rollupOptions: { input: fileURLToPath(new URL('src/main.ts', import.meta.url)), output: {
      inlineDynamicImports: true, entryFileNames: 'ssmt-dlss5-ui.js', assetFileNames: 'ssmt-dlss5-ui.[ext]',
    } },
  },
})
