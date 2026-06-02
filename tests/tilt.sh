#!/usr/bin/env bash
#
# tilt.sh — continuously feed wrist tilt-forward / tilt-back motion to a running
# Pebble emulator, to exercise BURR's shake-to-reveal.
#
# Start an emulator FIRST (in another terminal), e.g.:
#     pebble install --emulator basalt
# Then run this loop (Ctrl-C to stop):
#     ./tilt.sh            # tilt forward/back every 1s
#     ./tilt.sh 0.5        # ...every 0.5s
#     TAP=1 ./tilt.sh      # send accel TAPS instead of tilts
#
# Note: BURR's reveal is driven by the accel TAP service, so a sustained tilt
# does not always register as a tap. If the time isn't revealing, use TAP=1 —
# emu-tap injects a real tap event, which is what handle_tap() listens for.

set -uo pipefail

interval="${1:-1}"

cleanup() { printf '\nstopped.\n'; exit 0; }
trap cleanup INT TERM

if ! command -v pebble >/dev/null 2>&1; then
  echo "error: 'pebble' CLI not found on PATH." >&2
  exit 1
fi

# One forward+back cycle. Returns non-zero if the emulator isn't reachable.
send() {
  local a b
  if [ "${TAP:-0}" = "1" ]; then
    a=(emu-tap --direction y+); b=(emu-tap --direction y-)
  else
    a=(emu-accel tilt-forward); b=(emu-accel tilt-back)
  fi
  pebble "${a[@]}" || return 1
  sleep "$interval"
  pebble "${b[@]}" || return 1
}

mode=$([ "${TAP:-0}" = "1" ] && echo "taps (y+/y-)" || echo "tilt-forward/tilt-back")
echo "Sending ${mode} every ${interval}s to the running emulator. Ctrl-C to stop."

while true; do
  if ! send; then
    echo "command failed — is an emulator running?  (pebble install --emulator basalt)" >&2
    exit 1
  fi
  sleep "$interval"
done
