#!/usr/bin/env bash
#
# reveal.sh — trigger BURR's shake-to-reveal in a running Pebble emulator by
# injecting an accelerometer TAP, which is the event handle_tap() listens for
# (accel_tap_service_subscribe). A tilt/gravity change is NOT a tap, which is
# why tilt.sh alone won't reveal the time — use this instead.
#
# Start an emulator first (separate terminal):
#     pebble install --emulator basalt
# Then:
#     ./reveal.sh              # reveal once
#     ./reveal.sh loop         # keep revealing (tap every 2s)
#     ./reveal.sh loop 1       # keep revealing (tap every 1s)
#
# A single tap reveals the time for the configured "Reveal seconds" (default 3)
# then it hides again, so loop with an interval <= that to keep it on screen.

set -uo pipefail

dir="y+"   # any axis triggers the tap service; handle_tap() ignores direction

if ! command -v pebble >/dev/null 2>&1; then
  echo "error: 'pebble' CLI not found on PATH." >&2
  exit 1
fi

tap() {
  pebble emu-tap --direction "$dir" || {
    echo "tap failed — is an emulator running?  (pebble install --emulator basalt)" >&2
    exit 1
  }
}

if [ "${1:-}" = "loop" ] || [ "${1:-}" = "-l" ]; then
  interval="${2:-2}"
  cleanup() { printf '\nstopped.\n'; exit 0; }
  trap cleanup INT TERM
  echo "Revealing every ${interval}s (tap ${dir}). Ctrl-C to stop."
  while true; do
    tap
    sleep "$interval"
  done
else
  tap
  echo "Tapped once — the time should reveal for a few seconds."
fi
