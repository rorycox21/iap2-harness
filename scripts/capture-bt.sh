#!/bin/sh
# Capture PlayPort bt-bridge traffic alongside a btmon HCI trace.
# The resulting HCI capture is the input needed to fill in the UNVERIFIED
# iAP2 identification and Wi-Fi handoff payloads from a real exchange.
set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 <iphone-address>" >&2
  exit 2
fi

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PLAYPORT_DIR=${PLAYPORT_DIR:-"$HOME/Desktop/Projects/playport"}
BT_BRIDGE=${BT_BRIDGE:-"$PLAYPORT_DIR/macos/bt-bridge/bt-bridge"}
CAPTURE_DIR="$ROOT/captures"
TIMESTAMP=$(date '+%Y%m%d-%H%M%S')
CAPTURE_FILE="$CAPTURE_DIR/bt-$TIMESTAMP.log"

if ! command -v btmon >/dev/null 2>&1; then
  echo "btmon is required to capture HCI traffic" >&2
  exit 127
fi
if [ ! -x "$BT_BRIDGE" ]; then
  echo "bt-bridge not found or not executable: $BT_BRIDGE" >&2
  echo "Build it with: $PLAYPORT_DIR/macos/bt-bridge/build.sh" >&2
  exit 127
fi
mkdir -p "$CAPTURE_DIR"

echo "Writing HCI capture to $CAPTURE_FILE" >&2
btmon >"$CAPTURE_FILE" 2>&1 &
BTMON_PID=$!
cleanup() {
  kill "$BTMON_PID" 2>/dev/null || true
  wait "$BTMON_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM
"$BT_BRIDGE" --address "$1"
