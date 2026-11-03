import path from 'node:path'
import tailwindcss from '@tailwindcss/vite'
import react from '@vitejs/plugin-react'
import { defineConfig } from 'vite'

// During `npm run dev`, API calls are forwarded to the ESP32. Override with ESP32_URL=http://<ip>.
const esp32Url = process.env.ESP32_URL ?? 'http://govee.local'

export default defineConfig({
  plugins: [react(), tailwindcss()],
  resolve: {
    alias: {
      '@': path.resolve(__dirname, './src'),
    },
  },
  server: {
    proxy: {
      '/api': esp32Url,
    },
  },
  build: {
    outDir: '../data',
    emptyOutDir: true,
  },
})
