# Contributing

Keep changes scoped to one concern and preserve the separation between UI/domain logic, transport/application services, device/protocol adapters and operating-system adapters.

Before opening a pull request, run the checks that apply to the changed area:

```bash
npm run check
cmake -S backend-cpp -B build/backend -G Ninja
cmake --build build/backend
ctest --test-dir build/backend --output-on-failure
```

Do not commit generated build output, local databases, factory data, credentials, API keys, private addresses or vendor SDK binaries whose redistribution terms are unclear.
