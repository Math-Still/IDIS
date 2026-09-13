import { defineConfig } from 'vite';
import vue from '@vitejs/plugin-vue';

export default defineConfig({
  // 必须使用相对路径：同一份 dist 才能被 resource://rawfile/web/index.html 承载。
  base: './',
  plugins: [vue()],
  build: {
    target: 'es2020',
    sourcemap: false
  }
});
