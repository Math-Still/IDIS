# OpenHarmony / HongZOS Native Shell

This shell hosts the same Vue web application used by the browser deployment and provides a controlled boundary for native platform capabilities. Highest-priority device control remains in the C++ backend/runtime rather than in the UI shell.

`entry/src/main/resources/rawfile/web/` is a placeholder until the web build is prepared for the target package.

## Prepare the embedded web application

1. Build the Vue application:

```bash
npm ci
npm run build
```

2. Replace the placeholder content under:

```text
apps/openharmony-shell/entry/src/main/resources/rawfile/web/
```

with the contents of:

```text
apps/web/dist/
```

3. Provide the target `runtime-config.json` expected by the web application.
4. Build the HAP with the DevEco/HongZOS/OpenHarmony SDK and BSP for the actual target device.

Target SDK/API differences should be handled in the shell/provider binding layer, not by introducing OS-specific branches into the Vue business layer.
