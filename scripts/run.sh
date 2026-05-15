#!/usr/bin/env bash
# Start the proxy then launch VICE with User Port RS-232 on device 2 → TCP.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"
PRG="$ROOT/c64/build/claude64.prg"
BUILD_DIR="$ROOT/c64/build"
X64="${X64:-$(command -v x64sc)}"
PROXY_ARGS=()

VICE_FLAGS=(
  -default
  -busdevice8
  -fs8 "$BUILD_DIR"
  -fs8savep00
  -fs8convertp00
  -acia1
  -acia1mode 1
  -acia1irq 1
  -myaciadev 0
  -acia1base 0xDE00
  -rsdev1 127.0.0.1:25232
  -rsdev1baud 2400
  -rsdev1ip232
)

if [[ "${1:-}" == "--echo" ]]; then
  PROXY_ARGS=(--echo)
elif [[ -z "${ANTHROPIC_API_KEY:-}" ]]; then
  echo "error: ANTHROPIC_API_KEY is not set" >&2
  echo "tip: use '$0 --echo' to test without Anthropic" >&2
  exit 1
fi

if [[ ! -f "$PRG" ]]; then
  echo "error: $PRG not found — run 'make build' first" >&2
  exit 1
fi

if [[ -z "$X64" || ! -x "$X64" ]]; then
  echo "error: x64 not found" >&2
  exit 1
fi

# Start proxy in background
python3 "$ROOT/proxy/claude_proxy.py" ${PROXY_ARGS[@]+"${PROXY_ARGS[@]}"} &
PROXY_PID=$!
echo "proxy started (pid $PROXY_PID)"

cleanup() {
  kill "$PROXY_PID" 2>/dev/null || true
  echo "proxy stopped"
}
trap cleanup EXIT

# Wait until the proxy socket is actually bound before launching VICE
python3 -c "
import socket, time, sys
for _ in range(50):
    try:
        s = socket.create_connection(('127.0.0.1', 25232), timeout=0.1)
        s.close()
        sys.exit(0)
    except OSError:
        time.sleep(0.1)
sys.exit(1)
" || { echo "error: proxy did not start in time" >&2; exit 1; }

export GSETTINGS_SCHEMA_DIR=/opt/homebrew/share/glib-2.0/schemas

"$X64" \
  "${VICE_FLAGS[@]}" \
  -autostartprgmode 1 \
  "$PRG"
