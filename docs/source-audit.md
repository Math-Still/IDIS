# Source Publication Audit — 2026-09-13

## Input

Source material was supplied as a Windows/Docker delivery archive named `smart-factory-1.8.0-blue-wide-windows-deploy-20260913`.

The archive was a runnable delivery package rather than a clean development repository. In particular, root `package.json` commands referenced an internal `scripts/` directory that was not present in the archive.

## Removed from the public source tree

- `START.bat`, `STOP.bat`, `STATUS.bat` one-click deployment wrappers.
- `install*.log` build/install logs.
- `PACKAGE-OK.txt` and `RELEASE-MANIFEST.json` delivery markers.
- screenshot automation scripts and verification JSON.
- build-fix, Windows startup and delivery-validation notes tied to the packaged release process.
- obsolete Docker color-candidate/toolchain variants and duplicate local compose files.
- fixed plaintext development login tokens from the public repository path.
- npm commands whose implementation files were absent from the supplied archive.

## Retained

- Vue/TypeScript source and workspaces.
- Native C++ backend, CMake configuration and CTest suite.
- OpenHarmony/HongZOS shell source.
- target-OS/native/SSH deployment scaffolding.
- Dockerfile and local compose configuration.
- representative HMI screenshots.

## Secret review

The source tree was scanned for common key/password/token/private-key patterns, private IPv4 addresses, absolute Windows paths and email-address residue. No real API key, private key or private factory IP was identified. Optional DeepSeek integration reads `DEEPSEEK_API_KEY` from the environment; the example `.env` leaves it blank.

## Build/test verification

C++ verification was executed from source with CMake/Ninja and completed successfully:

- backend build: PASS
- CTest: **10 passed / 0 failed**

The original delivery material recorded successful `npm ci`, Vue type checking and Vite production build. During this publication pass, a fresh npm dependency install in the packaging environment exceeded the available network execution window, so frontend checks were not falsely marked as independently rerun.
