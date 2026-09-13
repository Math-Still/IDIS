# Native POSIX Deployment

Generic native deployment requires:

- C++20 compiler;
- CMake 3.20+;
- Ninja;
- Boost with Boost.JSON;
- SQLite3 development package;
- OpenSSL development package;
- POSIX scheduling/thread support for the generic adapter.

Build:

```bash
cmake -S backend-cpp -B build/native-backend -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DSMART_FACTORY_ENABLE_SIMULATED_ADAPTER=OFF
cmake --build build/native-backend
```

Run:

```bash
./deploy/native/run-native.sh /path/to/backend.factory.conf
```

If `require_realtime=true`, the backend refuses to continue when the required scheduling policy cannot be obtained. Map permissions and scheduling capabilities to the actual target OS/SDK rather than assuming Linux privilege semantics.
