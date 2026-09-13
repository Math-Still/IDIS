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

if command -v rsync >/dev/null 2>&1; then
  rsync -az --delete -e "ssh -p $PORT" \
    --exclude node_modules --exclude build --exclude .git \
    "$ROOT/" "$TARGET:$REMOTE_DIR/"
else
  tar --exclude=node_modules --exclude=build --exclude=.git -C "$ROOT" -czf - . \
    | ssh -p "$PORT" "$TARGET" "mkdir -p '$REMOTE_DIR' && tar -xzf - -C '$REMOTE_DIR'"
fi

ssh -p "$PORT" "$TARGET" "cd '$REMOTE_DIR' && cmake -S backend-cpp -B build/native-backend -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/native-backend -j2 && ctest --test-dir build/native-backend --output-on-failure"
