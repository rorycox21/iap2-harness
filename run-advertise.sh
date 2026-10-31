#!/bin/sh
# Advertise the receiver as a CarPlay/AirPlay device on the local network.
# Kill with Ctrl-C. The iPhone discovers it under Settings > General > CarPlay.
#
# The reference receiver advertises only its AirPlay endpoint. It discovers
# _carplay-ctrl._tcp services from the phone; it does not publish a local
# control service. Do not advertise port 7001 because this receiver has no
# listener there.
#
# Keep these identity values aligned with /info. Override them in the
# environment when the receiver is configured with a real Bluetooth identity.
DEVICE_ID="${CARPLAY_DEVICE_ID:-02:00:00:00:00:01}"
DEVICE_NAME="${CARPLAY_NAME:-MacCarPlay}"
MODEL="${CARPLAY_MODEL:-MacCarPlay,1}"
AIRPLAY_PORT="${CARPLAY_AIRPLAY_PORT:-7000}"

if command -v dns-sd >/dev/null 2>&1; then
  dns-sd -R "$DEVICE_NAME" "_airplay._tcp" local "$AIRPLAY_PORT" \
    deviceid="$DEVICE_ID" features=0x615653AEE2 model="$MODEL" \
    srcvers=950.7.1 flags=0x4 manufacturer=BMW &
elif command -v avahi-publish-service >/dev/null 2>&1; then
  avahi-publish-service "$DEVICE_NAME" "_airplay._tcp" "$AIRPLAY_PORT" \
    deviceid="$DEVICE_ID" features=0x615653AEE2 model="$MODEL" \
    srcvers=950.7.1 flags=0x4 manufacturer=BMW &
else
  printf '%s\n' 'No Bonjour/Avahi publisher available. Use --advertise-script PATH or --no-advertise with external discovery.' >&2
  exit 127
fi
AIRPLAY_PID=$!

cleanup() {
  kill "$AIRPLAY_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

wait "$AIRPLAY_PID"
