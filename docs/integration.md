# Integration Guide

## Integration boundaries

IDIS keeps target-specific code behind three boundaries:

- **DeviceAdapter** — acquisition and command execution for a concrete site/device family.
- **ProtocolAdapter** — protocol-specific transport and mapping details.
- **OsAdapter** — operating-system capabilities such as scheduling and SDK-specific services.

The supplied development configuration uses a simulated `DeviceAdapter`. Factory mode must use validated target adapters and must not silently fall back to simulation.

## Factory configuration

Start from the example files in `backend-cpp/config/` and `deploy/config/`. Keep site-specific copies outside version control.

Representative plugin settings:

```text
device_adapter=plugin
device_adapter_library=/opt/idis/lib/libfactory-device-adapter.so

os_adapter=plugin
os_adapter_library=/opt/idis/lib/libtarget-os-adapter.so
```

Do not place PLC register maps, passwords, API keys, real plant addresses or vendor credentials in the public repository.

## Native target build

```bash
cmake -S backend-cpp -B build/target -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DSMART_FACTORY_ENABLE_SIMULATED_ADAPTER=OFF \
  -DSMART_FACTORY_BUILD_TESTS=OFF \
  -DSMART_FACTORY_BUILD_TARGET_DIAGNOSTIC=ON
cmake --build build/target
```

Target diagnostic:

```bash
./build/target/smart-factory-target-diagnostic \
  --config /path/to/backend.factory.conf \
  --probe-realtime
```

## Minimum acceptance checks

1. Adapter ABI/API compatibility.
2. Complete acquisition-cycle semantics and stale/offline behavior.
3. Command whitelist, range validation and permission enforcement.
4. Field feedback and uncertain-outcome handling.
5. Alarm persistence and restart recovery.
6. Audit persistence and authentication-failure recording.
7. Realtime scheduling and privilege behavior on the target OS.
8. Device/protocol I/O timeout bounds.
9. HTTPS/WSS and origin policy for factory deployment.
10. Backup, restore, retention, disk-space and long-running soak behavior.
