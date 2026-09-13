# Deployment Notes

## Local Docker development

```bash
docker compose -f deploy/local/docker-compose.yml up -d --build
docker compose -f deploy/local/docker-compose.yml logs -f --tail=200
```

The local development stack exposes `127.0.0.1:8001`. It is a development path, not a factory deployment profile.

## Generic native target

```bash
cmake -S backend-cpp -B build/native-backend -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DSMART_FACTORY_ENABLE_SIMULATED_ADAPTER=OFF
cmake --build build/native-backend
./deploy/native/run-native.sh /path/to/backend.factory.conf
```

## HongZOS / OpenHarmony

Build the Vue web application first, copy the resulting `apps/web/dist/` content into the OpenHarmony shell `rawfile/web/` directory, provide a target runtime configuration, then build the HAP with the actual DevEco/HongZOS/OpenHarmony SDK and BSP supplied for the target device.

## Kylin / UOS

Build and serve the web application on the target or a local gateway, build the native backend against the target distribution, and validate browser kiosk behavior, REST/WebSocket connectivity, permissions and realtime capabilities on the actual system.

## Remote target workflow

The scripts in `deploy/ssh/` are intended for source synchronization, target compilation and diagnostics. Review target addresses and credentials before use; none are embedded in this repository.
