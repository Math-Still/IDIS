# UOS Deployment

1. Build the Vue application with `npm ci && npm run build`.
2. Serve `apps/web/dist/` through the selected local web server or the IDIS backend static-file path.
3. Build the C++ backend natively on a compatible UOS toolchain, preferably with `SMART_FACTORY_ENABLE_SIMULATED_ADAPTER=OFF` for factory use.
4. Provide a site-specific runtime configuration using the real backend address and `dataMode=backend`.
5. Validate REST/WebSocket connectivity, browser kiosk behavior, permissions, offline/recovery behavior and realtime scheduling on the actual target.

The optional `start-kiosk.sh` only launches a Chromium-compatible browser; it does not configure or validate the factory runtime.
