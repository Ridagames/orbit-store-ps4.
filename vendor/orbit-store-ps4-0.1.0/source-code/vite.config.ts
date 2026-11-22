import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import legacy from "@vitejs/plugin-legacy";
import network from "./config/network.json";
const backend = `http://127.0.0.1:${network.httpPort}`;
export default defineConfig({
  plugins: [react(), legacy({
    // PS4 browser versions vary by firmware. Serve local polyfills and a
    // transpiled fallback instead of inferring features from a firmware number.
    targets: ["Safari >= 10"],
    additionalLegacyPolyfills: ["core-js/proposals/global-this", "whatwg-fetch", "abortcontroller-polyfill/dist/polyfill-patch-fetch"],
  })],
  build: { cssTarget: "safari10", sourcemap: false },
  server: {
    port: 5177,
    strictPort: true,
    proxy: {
      "/api": {
        target: backend,
        changeOrigin: true,
        configure(proxy) {
          proxy.on("proxyReq", (outgoing, incoming) => {
            // Translate only a same-origin development request; keep hostile origins intact.
            if (incoming.headers.origin === `http://${incoming.headers.host}`)
              outgoing.setHeader("Origin", backend);
          });
        },
      },
    },
  },
});
