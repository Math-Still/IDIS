# HongZOS / OpenHarmony Deployment

The repository provides an application-shell and adapter boundary, not a pre-certified binary for every HongZOS/OpenHarmony device.

1. Build the Vue application with `npm ci && npm run build`.
2. Copy `apps/web/dist/` into the shell `rawfile/web/` directory and provide the target runtime configuration.
3. Build the HAP with the DevEco/HongZOS/OpenHarmony SDK and BSP supplied for the actual device.
4. Adapt SDK/API differences only in the shell/provider and OsAdapter boundaries.
5. Validate native bridge capability, device/network state, REST/WebSocket communication, restart behavior and target permissions on hardware.

Do not treat the placeholder `rawfile/web` content as a production application build.
