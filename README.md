# iap2-harness

An experimental macOS C++ receiver for exploring wireless CarPlay with a Mac window as the initial display target. The project currently builds locally; end-to-end behavior with a live iPhone is not yet verified.

## What it contains

- iAP2 message framing, link synchronization, and the Bluetooth RFCOMM bridge transport.
- MFi authentication using a caller-supplied certificate and private key.
- Wireless CarPlay control and Wi-Fi handoff messages reconstructed from the local DiPlay reference. These still need live-device validation.
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
