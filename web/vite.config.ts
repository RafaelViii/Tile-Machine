import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import tailwindcss from '@tailwindcss/vite';

export default defineConfig({
  plugins: [react(), tailwindcss()],
  // The Firebase SDK alone is ~450 kB minified; one bundle is fine for this dashboard.
  build: { chunkSizeWarningLimit: 800 },
});
