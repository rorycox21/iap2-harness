# Platform boundaries and porting

The receiver now separates portable protocol code from media output. This is
preparation for Linux/Pi and QNX/NBT ports; it does not implement their hardware
adapters. The macOS renderer remains the default on macOS.

## Targets

| Target | Responsibility | Platform dependencies |
| --- | --- | --- |
| `iap2_core` | iAP2, authentication, AirPlay control, crypto, screen/audio framing and POSIX network receivers | C++20, OpenSSL, Mbed TLS 3, threads, BSD sockets |
| `iap2_platform` | Selected video/audio/input backend and UI event loop | Apple frameworks only for `macos`; none for `headless`; target SDK for `custom` |
| `iap2_transport` | Optional experimental USB host transport | libusb only when `CARPLAY_ENABLE_USB=ON` |
| `iap2-harness` | Startup/configuration and composition of core/backends | POSIX process support |

Core tests inject a recording backend. They do not open an AppKit window or
require the Apple video/audio frameworks. The socket/process implementations
are POSIX implementations, not a promise that every QNX SDK supports the
current runtime/toolchain dependencies.

## Build choices

Keep dependency discovery through CMake imported targets and
`CMAKE_PREFIX_PATH` / `MbedTLS_DIR`. There is no hardcoded Homebrew include path.
Cross builds must use libraries compiled for the target architecture, not Mac
libraries. Mbed TLS 3 is required; distro Mbed TLS 2 packages are not sufficient.

Normal macOS (same runtime defaults as the verified receiver):

```sh
cmake -S . -B build -DCARPLAY_MEDIA_BACKEND=macos \
  -DCMAKE_PREFIX_PATH="$(brew --prefix mbedtls@3);$(brew --prefix openssl)" \
  -DMbedTLS_DIR="$(brew --prefix mbedtls@3)/lib/cmake/MbedTLS"
cmake --build build -j 4
ctest --test-dir build --output-on-failure
./build/iap2-harness --platform-info
```

Headless protocol build (on Linux, or to verify isolation on the Mac):

```sh
cmake -S . -B build-headless \
  -DCARPLAY_MEDIA_BACKEND=headless \
  -DCARPLAY_ENABLE_USB=OFF \
  -DCARPLAY_BUILD_MAC_HELPERS=OFF
cmake --build build-headless -j 4
ctest --test-dir build-headless --output-on-failure
./build-headless/iap2-harness --platform-info
```

Supply the appropriate crypto dependency prefixes when they are not installed
in CMake's search path. `headless` receives/decrypts protocol data but does not
display video, play audio or generate touch input. Its capability report and
startup message state that explicitly.

## Implementing a native backend

The contract is [MediaBackend](../include/platform/media_backend.hpp). Implement
`platform::createMediaBackend(DisplayConfig)` and return an owned backend.
[This compileable example](../examples/custom_media_backend.cpp) has no output
and can be used to check the build hook before adding a native implementation.

```sh
cmake -S . -B build-custom \
  -DCARPLAY_MEDIA_BACKEND=custom \
  -DCARPLAY_CUSTOM_MEDIA_SOURCE="$PWD/examples/custom_media_backend.cpp" \
  -DCARPLAY_ENABLE_USB=OFF -DCARPLAY_BUILD_MAC_HELPERS=OFF
```

`CARPLAY_CUSTOM_MEDIA_INCLUDE_DIRS` and `CARPLAY_CUSTOM_MEDIA_LIBRARIES` accept
CMake lists for target SDK includes/libraries. Set `CMAKE_TOOLCHAIN_FILE` using
your actual SDK/toolchain for a cross build. This example is not a working
Linux or QNX renderer.

- `run(task)` owns the event loop and runs networking without blocking UI input.
- `configureVideo` receives `H264`/`H265` and avcC/hvcC codec configuration.
- `submitVideo` receives a decrypted Annex-B access unit. Calls may arrive on
  the screen receiver's worker thread. Preserve decoder state across repeated
  identical codec configuration; `resetVideo` marks a new stream/session.
- `configureAudio` selects sample rate/channel count. `submitPcm16Le` receives
  interleaved signed 16-bit little-endian PCM; backends do not parse RTP or
  decrypt packets. PCM playback remains unverified with a real phone.
- `setTouchHandler` registers input in the advertised display's pixel coordinates.
  Clear callbacks and release devices safely during session teardown.
- `displayConfig` supplies the geometry used by `/info`, safe areas and touch HID.
  Media capabilities describe actual physical output/input support. Headless
  can still accept/decrypt video for protocol diagnostics.

Linux still needs a BlueZ pairing/RFCOMM helper, a decoder/display implementation
(e.g. FFmpeg/GStreamer with an appropriate output), ALSA/PipeWire output and
input integration. QNX still needs the unit-specific media/Bluetooth/Wi-Fi APIs,
compatible crypto libraries and a suitable compiler/runtime. Original NBT CPU,
ABI, USB capabilities and SDK version must be measured first. Older QNX SDKs
may require a separate C++ language/runtime backport; changing the CMake backend
alone cannot provide that. No physical Pi/NBT validation is claimed.

## Bluetooth, discovery and hotspot setup

`--bt-bridge PATH` (or `CARPLAY_BT_BRIDGE`) selects an external full-duplex helper.
It must accept `--address PHONE`, forward bytes on stdin/stdout and put diagnostic
output on stderr. The macOS default is the `bt-bridge` next to the receiver
binary. No path into a developer's Desktop checkout is used. Native BlueZ/QNX
helpers can use this contract without changing the protocol loop. Bluetooth
pairing and radio advertisement are still the helper/platform's responsibility.

The Python launcher, native pairing app and hotspot probe remain **macOS setup
tools**. A Linux/QNX deployment must provide its own setup rather than invoke
those Mac tools.

`run-advertise.sh` selects `dns-sd` or `avahi-publish-service`. It is a POSIX shell
script. `--advertise-script PATH` selects another publisher script;
`--no-advertise` allows externally managed discovery. Identity/TXT values must
match the AirPlay identity used by the receiver. The script checks for a tool;
Avahi daemon availability still depends on the target deployment.

The hotspot remains outside the protocol engine. Supply SSID, passphrase,
channel and reachable address explicitly through the existing CLI/environment.
No backend changes the host's Wi-Fi settings implicitly.

## State, USB and display settings

Trust-state defaults preserve the working Mac identity path. On non-Apple
systems they use `$XDG_STATE_HOME/iap2-harness` when absolute, otherwise
`$HOME/.local/state/iap2-harness`. `CARPLAY_PAIRING_FILE` overrides either.
Choose a writable private location on an embedded target; never commit it.

USB can be disabled entirely. When enabled, the host transport respects
`--interface`, endpoint arguments and optional `--usb-config N`; configuration
0 preserves the device's current setting. It does not force an iPhone-specific
configuration or detach kernel drivers. This is not the USB role-switch/NCM
implementation needed for true wired CarPlay.

The default display remains 1024×600 at up to 60 fps. Optional arguments:

```sh
--display-width 1280 --display-height 750 --max-fps 30
```

These values change the advertised display, safe area, touch descriptor and
Mac input mapping together. The phone must accept the requested geometry.
Higher resolution costs bandwidth/decoding work and needs live validation.
Changing a window's size alone cannot increase the encoded stream resolution.

## Video artifact fixes

A length-prefixed NAL of 300 bytes begins `00 00 01 2c`. The old heuristic
mistook that length for an Annex-B start code. The screen receiver now follows
the codec configuration's NAL length size and parses lengths explicitly.
Regression tests cover that ambiguity, multiple NALs and malformed/truncated
samples. The Mac decoder also preserves repeated codec configuration, drains
old decoder work, rejects stale queued presentation callbacks and releases
per-frame NSImage ownership. Display interpolation is set to high quality.
The macOS and headless configurations were built/tested on the Mac. After a
live restart on 31 October 2026, the test user reported cleaner image output.
Actual Pi/QNX builds and H.265 output remain unverified. The prior screenshot
documents the pre-fix output.
