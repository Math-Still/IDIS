#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 2 ]]; then
  echo "usage: $0 user@host remote_dir [ssh_port] [config_path]"
  exit 2
fi
TARGET="$1"
REMOTE_DIR="$2"
PORT="${3:-${SSH_PORT:-22}}"
CONFIG="${4:-backend-cpp/config/backend.development.conf}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

if command -v rsync >/dev/null 2>&1; then
  rsync -az --delete -e "ssh -p $PORT" \
    --exclude node_modules --exclude build --exclude .git --exclude data/historian --exclude data/audit \
    "$ROOT/" "$TARGET:$REMOTE_DIR/"
else
  tar --exclude=node_modules --exclude=build --exclude=.git --exclude=data/historian --exclude=data/audit -C "$ROOT" -czf - . \
    | ssh -p "$PORT" "$TARGET" "mkdir -p '$REMOTE_DIR' && tar -xzf - -C '$REMOTE_DIR'"
fi

ssh -p "$PORT" "$TARGET" "cd '$REMOTE_DIR' && \
  cmake -S backend-cpp -B build/target-diagnostic -G Ninja \
    -DSMART_FACTORY_ENABLE_SIMULATED_ADAPTER=OFF \
    -DSMART_FACTORY_BUILD_TESTS=OFF \
    -DSMART_FACTORY_BUILD_TARGET_DIAGNOSTIC=ON && \
  cmake --build build/target-diagnostic --target smart-factory-target-diagnostic -j2 && \
  ./build/target-diagnostic/smart-factory-target-diagnostic --config '$CONFIG' --probe-realtime"
