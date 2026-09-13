# Security Policy

## Public repository rules

This repository intentionally contains no real factory credentials, reusable default login tokens, private API keys, SSH keys or production network addresses.

Development authentication is disabled by default in the public configuration. If authenticated local testing is required, create an untracked `backend-cpp/config/auth.local.json` from the provided factory-schema example and point a local configuration file to it.

## Production deployment baseline

Before a factory deployment:

- enable authentication and use independently generated high-entropy credentials;
- store secrets outside Git and inject them through environment or platform secret management;
- use HTTPS/WSS and a restricted origin policy;
- disable the simulated device adapter;
- validate real DeviceAdapter / ProtocolAdapter / OsAdapter implementations on the target hardware;
- verify realtime scheduling privileges and failure behavior;
- review exposed ports, service accounts, filesystem permissions, persistence and retention;
- preserve independent PLC/DCS/SIS/ESD safety authority for safety-critical actions;
- verify backup, restore, audit and incident-recovery procedures.

## Reporting

Do not place real credentials, factory topology or exploitable security details in a public issue. Use the repository owner's private contact path or GitHub private vulnerability reporting when available.
