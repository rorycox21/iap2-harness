# Change notes

## 31 October 2026

### Portability preparation

- Injected a media backend into the protocol engine; Apple UI/audio frameworks
  are isolated in the macOS adapter.
- Added headless and custom backend selection plus a compilable custom example.
- Made USB and Swift helpers optional; removed hardcoded Homebrew include and
  developer Desktop helper paths.
- Added portable trust-state paths, configurable display dimensions/frame rate,
  external discovery selection and dns-sd/Avahi publishing support.
- Documented backend contracts and the remaining Pi/Linux and QNX/NBT work.
  These platforms do not yet have native hardware adapters.

### Video quality

- Parse codec-specified NAL lengths instead of guessing from start-code bytes;
  regression coverage includes the ambiguous 256–511-byte NAL range.
- Preserve decoder state for identical codec configuration, reject stale queued
  presentation callbacks and release per-frame image ownership.
- Use high-quality Mac display interpolation. The user restarted the receiver
  and confirmed cleaner output; the updated live screenshot is in the README.

### Validation and limits

- macOS, headless and custom-example builds passed tests on the Mac during
  the refactor. This does not establish physical Pi or QNX compatibility.
- Four asynchronous iAP2 status families (Now Playing, power, communications
  and call state) remain pending; they are kept out of authentication handling.
- Audio/touch live validation, microphone/Siri, physical media/knob controls,
  true wired CarPlay and automatic hotspot/reconnect recovery remain open.

## 30 October 2026

- Confirmed live iPhone CarPlay video in the native macOS receiver window.
- Validated outgoing Bluetooth iAP2, MFi authentication, identification, Wi-Fi
  handoff, AirPlay pairing/encrypted control and H.264 decode/display.
- Added a fresh-device startup guide, native pairing flow, Mac hotspot checks
  and the original live screenshot.
