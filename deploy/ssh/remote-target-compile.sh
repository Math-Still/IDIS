#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 2 ]]; then
  echo "usage: $0 user@host remote_dir [ssh_port]"
  exit 2
fi
TARGET="$1"
REMOTE_DIR="$2"
PORT="${3:-${SSH_PORT:-22}}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

rsync -az --delete -e "ssh -p $PORT" \
  --exclude node_modules --exclude build --exclude .git --exclude data/historian --exclude data/audit \
  "$ROOT/" "$TARGET:$REMOTE_DIR/"

ssh -p "$PORT" "$TARGET" "cd '$REMOTE_DIR' && \
  cmake -S backend-cpp -B build/target-production -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DSMART_FACTORY_ENABLE_SIMULATED_ADAPTER=OFF \
    -DSMART_FACTORY_BUILD_TESTS=OFF \
    -DSMART_FACTORY_BUILD_TARGET_DIAGNOSTIC=ON && \
  cmake --build build/target-production -j2"
