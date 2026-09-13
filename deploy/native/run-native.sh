#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build/native-backend}"
CONFIG="${1:-$ROOT/backend-cpp/config/backend.development.conf}"
exec "$BUILD_DIR/smart-factory-backend" --config "$CONFIG"
