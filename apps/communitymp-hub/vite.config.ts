import { svelte } from "@sveltejs/vite-plugin-svelte";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { defineConfig } from "vite";

const appRoot = dirname(fileURLToPath(import.meta.url));
const repoRoot = resolve(appRoot, "../..");

export default defineConfig({
  plugins: [svelte()],
  clearScreen: false,
  server: {
    fs: {
      allow: [appRoot, repoRoot]
    },
    strictPort: true,
    host: "127.0.0.1",
    port: 1420
  }
});
