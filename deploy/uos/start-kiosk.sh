#!/usr/bin/env bash
set -euo pipefail
URL="${SMART_FACTORY_URL:-http://127.0.0.1:8080/}"
BROWSER=""
for candidate in chromium chromium-browser google-chrome; do
  if command -v "$candidate" >/dev/null 2>&1; then BROWSER="$candidate"; break; fi
done
if [[ -z "$BROWSER" ]]; then
  echo "No Chromium-compatible browser found. Install/locate Chromium and retry." >&2
  exit 2
fi
exec "$BROWSER" --kiosk --no-first-run --disable-session-crashed-bubble "$URL"
