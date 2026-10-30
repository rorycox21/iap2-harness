# Wireless CarPlay on Mac — plan

## Goal

Get the iPhone to establish a wireless CarPlay session with this Mac receiver and render the CarPlay UI in a native Mac window. Treat this as a sequence of independently observable milestones.

## Current position

The project builds on macOS. It has an iAP2 link handshake, an MFi signer loader and signing implementation, source-derived identification/authentication/control messages, Bluetooth bridge transport, a Wi-Fi handoff path, and AirPlay pairing/media receive paths. The video window and audio output are implemented. None of the full live-iPhone path has been verified end to end.

Bluetooth pairing is the current blocker. In recent approved attempts, the iPhone displayed the numeric confirmation and the user approved it, but the pairing session ended with HCI status `0x1f` (“Unspecified Error”) and the iPhone did not appear in the Mac's paired-device list. That status is a catch-all, not a diagnosis.

## Milestones

### 1. Diagnose Bluetooth pairing

- Capture one controlled pairing attempt with Apple's PacketLogger (from Additional Tools for Xcode). Record the exact timestamp, device address, phone-side prompt/result, bridge callbacks, and final paired-list state.
- Keep the iPhone awake and unlocked with Settings → Bluetooth open; keep it close to the Mac. Confirm Bluetooth permission is granted to the terminal app running the bridge.
- Check whether pairing completes at the Bluetooth controller level and whether macOS stores a bond. Do not infer success from the numeric-code prompt alone.
- If it fails, use the trace to decide whether to fix the Mac-side pairing helper or implement the CarPlay accessory discovery/advertising flow. Repeated retries without a changed condition are unlikely to add information.

**Exit:** the phone is bonded and listed by the bridge, or the trace establishes a specific failure point and a tested corrective change.

### 2. Prove RFCOMM and iAP2 link bring-up

- Open the iAP2 RFCOMM service through `bt-bridge`.
- Send the link synchronization/SYN exchange and capture the first phone bytes.
- Save a sanitized raw capture with iPhone model and iOS version; remove addresses or identifiers before sharing publicly.

**Exit:** RFCOMM opens reliably and the phone's iAP2 link response is repeatable.

### 3. Validate identification and MFi authentication

- Compare the captured message order and payloads with the current source-derived implementation.
- Confirm the phone accepts identification and the supplied MFi certificate/signature exchange.
- Make corrections only from observed bytes or authoritative reference behavior; keep unverified fields marked clearly.

**Exit:** the phone reaches successful iAP2 authentication and asks for wireless configuration.

### 4. Establish the Wi-Fi handoff

- Provide a Wi-Fi access point reachable by both phone and Mac, with explicit SSID, security, passphrase, channel, and Mac-side IP.
- Verify the phone accepts the handoff and associates with the intended network.
- Confirm IP reachability and Bonjour discovery before debugging AirPlay.

**Exit:** the phone joins the receiver network and connects to the advertised AirPlay endpoint.

### 5. Bring up AirPlay and render the first frame

- Validate RTSP pairing and encrypted control against a live session.
- Confirm screen stream key derivation, frame decryption, VideoToolbox decoding, and native-window display.
- Debug audio, touch, and other controls only after video works.

**Exit:** a live iPhone CarPlay UI appears in the Mac window.

### 6. Harden and prepare for QNX

- Add repeatable logs and capture handling, connection cleanup, reconnect behavior, and bounded resource use.
- Separate macOS Bluetooth, Wi-Fi, video, and audio platform code from protocol code.
- Cross-compile and validate each hardware-facing component against the target QNX toolchain and NBT interfaces.

## Immediate next action

Use PacketLogger for one pairing attempt and inspect whether the controller rejects pairing before or after confirmation. `scripts/capture-bt.sh` invokes BlueZ `btmon`, which is appropriate on Linux systems with BlueZ, not for capturing the Mac's Bluetooth controller. Do not change Internet Sharing or hotspot settings until a network plan is selected and the user is ready to enable it.

## Explicitly out of scope for the first Mac milestone

QNX/NBT integration, Wi-Fi hotspot automation, unsupported audio codecs, microphone capture, and production reliability.
