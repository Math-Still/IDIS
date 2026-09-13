# IDIS Native C++ Backend

The C++ backend is the primary runtime for application services, transport, persistence, device integration and realtime/non-realtime command execution. Python is not required for the normal runtime path; the optional pybind11 bridge is disabled by default.

## Runtime domains

- **Non-Realtime** — query, persistence, reporting, configuration and general application work.
- **Realtime** — bounded, higher-priority execution for validated control requests.
- **Emergency** — separately bounded, highest-priority command class within the application runtime.
- **DeviceAdapter** — site/device acquisition and command boundary.
- **ProtocolAdapter** — protocol/vendor implementation boundary.
- **OsAdapter** — operating-system capability boundary.

The development build can compile the simulated DeviceAdapter. Factory builds should disable it explicitly.

## Build and test

```bash
cmake -S backend-cpp -B build/backend -G Ninja
cmake --build build/backend
ctest --test-dir build/backend --output-on-failure
```

Production-oriented build:

```bash
cmake -S backend-cpp -B build/target -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DSMART_FACTORY_ENABLE_SIMULATED_ADAPTER=OFF \
  -DSMART_FACTORY_BUILD_TESTS=OFF
cmake --build build/target
```

## Storage maintenance

```bash
./build/backend/smart-factory-storage-maintenance backup data/historian data/backup/historian
./build/backend/smart-factory-storage-maintenance verify data/backup/historian/historian-<timestamp>.sqlite3
# Stop the backend before restore.
./build/backend/smart-factory-storage-maintenance restore <backup.sqlite3> data/historian
```

## Target diagnostic

When built with `SMART_FACTORY_BUILD_TARGET_DIAGNOSTIC=ON`:

```bash
./build/target/smart-factory-target-diagnostic \
  --config /path/to/backend.factory.conf \
  --probe-realtime
```

See [`../docs/integration.md`](../docs/integration.md) for target adapter and acceptance guidance.
