# Target Environment Notes

Use this area for target-specific configuration and integration notes that do not belong in the common application/domain layer.

Recommended target workflow:

1. build the backend natively with the simulated adapter disabled;
2. run `smart-factory-target-diagnostic --probe-realtime` on the actual target;
3. load the validated DeviceAdapter/OsAdapter plugin libraries;
4. verify REST/WebSocket, persistence, command feedback, realtime scheduling and restart behavior;
5. keep factory credentials and network addresses outside the public repository.

See `../native/README.md` and `../../docs/integration.md`.
