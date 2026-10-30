# Earlier development notes

These notes preserve the prototype history. For the current startup workflow, see the root README.

# iap2-harness

An experimental macOS C++ receiver for exploring wireless CarPlay with a Mac window as the initial display target. Live CarPlay video was confirmed in the native Mac window on 30 October 2026 using a Mac-hosted Wi-Fi hotspot and the outgoing Bluetooth bridge. Audio and touch remain unverified.

## What it contains

- iAP2 message framing, link synchronization, and the Bluetooth RFCOMM bridge transport.
- MFi authentication using a caller-supplied certificate and private key.
- Wireless CarPlay control and Wi-Fi handoff messages reconstructed from the local DiPlay reference. Authentication, identification and Wi-Fi handoff passed live-device testing.
- An AirPlay RTSP server with pair-setup/pair-verify, encrypted control, video and PCM audio receive paths, and a native macOS video window.
- A raw `--pipe` capture mode and a full-duplex `--capture` mode for collecting the iPhone's initial iAP2 response.

This is a development harness, not a production receiver. Several protocol paths are source-derived but unverified against a live phone. See [STATUS.md](STATUS.md) for the detailed implementation state.

## Build on macOS

The current CMake project uses C++20 and requires OpenSSL, Mbed TLS 3.x, pkg-config, and libusb. The Mbed TLS setup used for the current build is:

```sh
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="$(brew --prefix mbedtls@3)" \
  -DMbedTLS_DIR="$(brew --prefix mbedtls@3)/lib/cmake/MbedTLS"
cmake --build build
```

The project is intended to keep its Mbed TLS 3.x configuration when cross-compilation support is added.

## Run modes

```sh
./build/iap2-harness --simulate
./build/iap2-harness --pipe
./build/iap2-harness --capture --address <iphone-bt-address>
./build/iap2-harness --address <iphone-bt-address> \
  --mfi-cert <certificate.der> --mfi-key <private-key.der>
```

`--simulate` exercises the authentication state machine with a mock signer. `--pipe` sends the link SYN and hex-logs bytes received on stdin. A one-way shell pipeline cannot carry that SYN back to the Bluetooth bridge; use `--capture --address` for the bridge's full-duplex process transport. The default bridge path points into `~/Desktop/Projects/playport`; override it with `--bt-bridge PATH` if needed.

The credential loader expects a DER PKCS#7 certificate bundle and matching DER PKCS#8 private key. Keep these files outside the repository. Never add credentials or captures containing private material to Git.

## Bluetooth troubleshooting

Wireless CarPlay uses Bluetooth for discovery, pairing/reconnection, iAP2 negotiation, authentication, and Wi-Fi handoff. Wi-Fi carries the AirPlay audio/video session. Apple describes the receiver as a CarPlay-capable Bluetooth accessory with service discovery, iAP2, and CarPlay capability advertising; a generic Mac-to-iPhone Bluetooth bond alone does not implement that receiver behavior ([Apple WWDC17](https://developer.apple.com/videos/play/wwdc2017/717/)).

In the latest pairing attempts, the phone showed a confirmation code and the user approved it, but macOS reported HCI status `0x1f` and no paired-device entry appeared. Bluetooth status `0x1f` is the generic “Unspecified Error,” so it does not identify which side or step failed ([Bluetooth Core Specification](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-60/out/en/architecture%2C-change-history%2C-and-conventions/controller-error-codes.html)). The next useful diagnostic is one PacketLogger trace of a single pairing attempt. PacketLogger is distributed by Apple in Additional Tools for Xcode ([Apple Bluetooth tools](https://developer.apple.com/bluetooth/)). The included `scripts/capture-bt.sh` uses Linux BlueZ `btmon`; it is not the macOS HCI capture method.

## Project plan

See [PLAN.md](PLAN.md) for sequenced milestones from Bluetooth diagnostics through Wi-Fi handoff and the first CarPlay frame.

## Experimental Bluetooth accessory mode

On macOS the normal CMake build also creates `build/bt-accessory`. It publishes
transient accessory-side iAP2 (`...caff`) and CarPlay SDP records, registers
incoming RFCOMM callbacks on their assigned channels, and removes the records
on timeout, SIGINT, SIGTERM or transport shutdown. It does not initiate pairing.

```sh
# Probe registration for 30 seconds (diagnostics on stderr).
./build/bt-accessory --advertise --seconds 30
# Optional temporary 120-second car-audio class lease; restored on exit.
./build/bt-accessory --advertise --seconds 30 --car-class
# Receive only from your selected phone and send the harness SYN when it connects.
./build/iap2-harness --capture --address <iphone-bt-address> \
  --bt-bridge ./build/bt-accessory
```

Keep macOS Bluetooth settings open for system-managed discoverability and select
the Mac from the iPhone if it appears. The helper leaves the system name and Wi-Fi
settings alone. Its default deadline is 120 seconds; `--seconds` accepts 5–120.
Only `--address` mode forwards binary stdio, and it rejects other peers and second
connections. Queued input is read only after the selected phone opens a channel.
The existing bridge interface can also select this helper for `--receiver` mode;
MFi credentials and a reachable Wi-Fi network are still required.

A successful local SDP registration does **not** verify on-air CarPlay EIR,
phone discovery, pairing, or a working CarPlay session. macOS may accept a
class-change request without exposing a useful readback. Check an independent
Bluetooth trace before treating discovery as complete. This prototype accepts
incoming channels; the established outgoing PlayPort transport remains available.

The service UUIDs and receiver roles were cross-checked against the public
[LIVI SDP definitions](https://github.com/pukikiko/LIVI-Lite/blob/main/native/livi-helperd/crates/livi-bt-host/src/sdp.rs)
and [Apple wireless CarPlay architecture](https://developer.apple.com/videos/play/wwdc2017/717/).

## Native Mac pairing app

If the CLI pairing helper times out at numeric confirmation, build and launch
Apple's GUI pairing flow from a real app bundle:

```sh
./scripts/mac-pairing/build.sh "$PWD/build/iAP2-Pairing.app"
open ./build/iAP2-Pairing.app
```

Keep the iPhone Bluetooth settings open, choose the phone in Apple's dialog, and
approve matching codes. The app checks both `isPaired` and membership in the
saved device list; it writes `pairing-app-result.log` beside the app bundle.
The bundle has its own local identity and Bluetooth usage descriptions and is
ad-hoc signed. It uses IOBluetoothUI's IOBluetoothPairingController instead of
the CLI's direct IOBluetoothDevicePair callbacks. This GUI route established a
saved bond with the development iPhone on 30 October 2026 after CLI timeouts.
The current diagnostic check is scoped to the development phone's address;
update `scripts/mac-pairing/main.m` for another device before building.

## Authentication-only live probe

After establishing a Bluetooth bond, `--auth-only --address PHONE --mfi-cert
FILE --mfi-key FILE [--bt-bridge PATH]` runs the real link and MFi exchange and
exits after the phone sends AuthenticationSucceeded. It defers Wi-Fi handoff.
MFi v3 signing accepts the 32-byte digest directly; the certificate response
preserves the DER PKCS#7 container. This probe passed against the development
iPhone on 30 October 2026. Passing it does not validate AirPlay or media.

## Single-command pairing and capture

The launcher checks the selected saved bond and opens Apple's native pairing
dialog if no bond is saved. `--cli-pair` enables the experimental direct API
first; it still returned `0x1f` in current testing. Keep iPhone
Settings → Bluetooth open and accept matching confirmation codes. It starts
exactly one full-duplex bridge:

```sh
python3 scripts/carplay.py --capture --address AA:BB:CC:DD:EE:FF \
  --bt-bridge ~/Desktop/Projects/playport/macos/bt-bridge/bt-bridge
```

Use `--binary /absolute/path/iap2-harness` for a build outside `./build`.
For the full receiver, replace `--capture` with `--receiver` and append the
usual MFi and Wi-Fi options; supply `CARPLAY_WIFI_PASSPHRASE` through the
environment. A saved bond is a prerequisite, not proof of a successful link.
The launcher does not remove stale bonds automatically.

Do not pipe `bt-bridge` into `--capture --address`: capture already launches
its own bidirectional bridge. Capture only collects protocol traffic; it does
not start a CarPlay video session.

## Mac-hosted Wi-Fi

Configure System Settings → General → Sharing → Internet Sharing to share a
live Ethernet connection **to Wi-Fi**. Wi-Fi Options can use `MacCarPlay`, a
fixed channel and WPA2/WPA3 Personal. The receiver does not need internet
access for its local media connection, but an enabled sharing switch alone
is not proof that macOS has started the access point.

Run `python3 scripts/mac_hotspot.py` to verify the active AP and its bridge IP.
Add `--hotspot` to `scripts/carplay.py --receiver ...` to derive the SSID, IP,
channel and security from that configuration. Supply the existing hotspot
password using `CARPLAY_WIFI_PASSPHRASE`; the launcher does not read or log it.
Hotspot mode refuses to use a normal Wi-Fi client address as an AP endpoint.
It checks an already enabled system hotspot; it does not yet toggle Internet
Sharing automatically. Do not use the shared-network password for MacCarPlay.
