# Project status

## Portability and video refactor — 31 October 2026

Work is on `refactor/platform-backends`. Protocol/session code now receives a
media backend; Apple UI/audio frameworks are linked only by the macOS adapter.
Headless and custom-backend build options, optional USB/Swift helpers, portable
trust-state paths, configurable display geometry, and external discovery/helper
selection form the porting boundary. Native Linux/Pi and QNX/NBT adapters still
need implementation and target-device validation.

The screen receiver now parses NAL lengths instead of guessing Annex-B framing
from leading bytes; 256–511-byte NALs were ambiguous under the old heuristic.
Regression tests cover that case. Duplicate Mac codec config no longer resets
the decoder; old presentation callbacks are guarded and per-frame NSImage
ownership is released. The test user restarted the Mac receiver and confirmed cleaner output.
Mac, headless and custom-example backend configurations built and passed tests
on the Mac. The custom example is explicitly a no-output skeleton; no Linux
or QNX hardware implementation is implied.

## Live CarPlay video confirmed — 30 October 2026

The user confirmed the CarPlay interface is visible in the native Mac receiver
window. The receiver decrypted more than 900 H.264 frames and maintained the
AirPlay control session. The working network is the Mac-hosted MacCarPlay AP,
channel 11, bridge100 / 192.168.2.1, with the Pi supplying Ethernet upstream
(10.42.0.1; Mac DHCP lease 10.42.0.201). The iPhone used 192.168.2.2.

Verified live: Bluetooth RFCOMM, iAP2 synchronization, MFi authentication,
accepted accessory identification, Wi-Fi handoff, AirPlay pair-setup,
pair-verify, encrypted control, MFi `/auth-setup`, initial SETUP, RECORD,
screen stream SETUP, H.264 decryption, decoding and visible display.
Audio playback and touch interaction have not been validated in this session.
Non-authentication status notifications such as 0x5001 are now kept out of the
authentication state machine, eliminating misleading authentication warnings.
Raw received message IDs remain available in the diagnostic log.

The final blockers were an unimplemented RECORD method and a listening socket
inherited by the Bluetooth helper. RECORD now returns 200 and RTSP responses
echo CSeq. TCP sockets use FD_CLOEXEC so the helper cannot keep port 7000 bound
when the control session closes. Connections with no first request time out
after ten seconds, preventing an idle Safari preconnect from monopolizing the
single-session server. A regression test replays the observed RECORD request
with fragmented input and checks its response and sequence number. Core tests
passed after these fixes. The helper also retains the selected device, opens
authenticated base connections, guards reentrant callbacks, and lets RFCOMM
close callbacks finish on EOF/SIGTERM/SIGINT.

The running secure launcher is the session workspace's
`work/start-hotspot-receiver.py`; it keeps the entered hotspot password only in
memory and supports controlled SIGUSR1 restarts. No hotspot password is stored
in this repository. The current receiver and hotspot have been left running.

## Mac hotspot validation — 30 October 2026

The Pi now supplies Ethernet using NetworkManager shared mode. The Mac's USB
Ethernet service was changed from disabled/static to enabled/DHCP after saving
its previous configuration in the session workspace. It received 10.42.0.201
with gateway 10.42.0.1. Internet Sharing is configured from USB Ethernet to
Wi-Fi, SSID MacCarPlay, fixed channel 11, WPA2/WPA3 Personal. The active ap1
interface is a member of bridge100, with receiver address 192.168.2.1.
`scripts/mac_hotspot.py` verified this actual AP endpoint. The earlier shared-network
address and password must not be used for this receiver-hosted network.

Fresh native pairing succeeded again, followed by successful iAP2 MFi
and identification exchanges. The phone accepted Use CarPlay and requested
a session. The next live test uses the active Mac hotspot and its existing
password, entered through a local secure prompt. First video remains unverified.

The iPhone has since joined MacCarPlay manually and confirmed that its CarPlay
entry is listed and enabled. Toggling Bluetooth off/on on the iPhone cleared
the stale RFCOMM channel. The subsequent hotspot-mode test completed RFCOMM,
iAP2 synchronization, MFi authentication, accepted identification and
CarPlayStartSession advertising 192.168.2.1:7000. No AirPlay TCP connection
arrived during that attempt. After a user-requested pause, the hotspot and
AirPlay listener remain active; Bluetooth closed. The next diagnostic is a
direct iPhone request to the AirPlay endpoint to separate LAN reachability
from the phone's automatic CarPlay startup.

Earlier reconnect failure diagnosis: Reconnection currently fails before iAP2: the
Bluetooth daemon reports `OI_RFCOMM_Connect failed (status=911)` / `0x38f`.
Published OpenInterface status definitions identify 911 as `OI_RFCOMM_DLCI_EXISTS`;
a stale RFCOMM channel is the current diagnosis to test. A targeted
`IOBluetoothDevice.closeConnection` returned success but did not clear that
error. The helper now guards reentrant base-connection callbacks and closes its
RFCOMM channel on SIGTERM/SIGINT. The iAP2 startup deadline is 60 seconds because
one real RFCOMM open completed after the former 15-second deadline.


## Latest connection work — 30 October 2026

Earlier live testing progressed through MFi authentication, accepted accessory
identification, Wi-Fi handoff, AirPlay pair-setup/pair-verify and encrypted RTSP.
The phone requested `/auth-setup`, which was previously unimplemented. That
handler is now implemented and covered by a peer-side cryptographic round-trip
check. It still needs live validation; no CarPlay frame has been confirmed.

The current reconnect attempt stalled in Bluetooth service discovery. macOS
had a saved test-iPhone bond while the iPhone had no Mac entry. Only this test bond
was removed; fresh pairing is in progress. Direct-address pairing reached
numeric comparison but returned `0x1f` both from the CLI and from an AppKit
app; the native pairing controller remains the demonstrated successful path. The AirPlay listener now remains
available beyond the previous one-minute accept deadline. Build and core tests
pass. `scripts/carplay.py` automates bond checks, pairing initiation/fallback
and starting exactly one bridge; the phone must still approve pairing.

## Earlier authentication validation — 30 October 2026

Apple's native IOBluetoothUI pairing controller, hosted in the new AppKit bundle
(`scripts/mac-pairing`), established a saved bond with the development iPhone
where the CLI helper repeatedly timed out at numeric confirmation. Outgoing
RFCOMM opened and the phone supplied its marker and synchronization packet.

The `--auth-only` live probe now completes successfully: AA00 → AA01 → AA02
(32-byte digest) → AA03 (64-byte P-256 signature) → AA05. It closes before Wi-Fi
handoff. This required fixing incremental SYN parsing, ACK sequence semantics,
control session 10 and CSM 0x4040 framing, certificate-only PKCS#7 loading and
preserving the container on the wire, and signing the supplied MFi v3 digest
without hashing it again. The core suite passes 58 checks; an independent
synthetic-identity check verifies the prehashed signature and container bytes.

Identification acceptance, Wi-Fi handoff, AirPlay negotiation and first frame
are not yet verified by this successful authentication-only probe.

## Current state

The C++20 project builds on macOS with the existing Mbed TLS 3.x configuration. It has an RTSP `/info` binary plist handler, an mDNS advertisement script, TCP, pipe, and PlayPort `bt-bridge` transports, the iAP2 message codec, authentication state machine, and a link SYN packet verified against the decompiled DiPlay reference. The active data path now wraps CSM messages in iAP2 link packets and uses the reference additive checksums. The vendored `airplay_crypto` implementation provides SRP client, X25519, Ed25519, ChaCha20, HKDF, TLV8, and binary plist support using Mbed TLS.

`OpenSslSigner::load` loads the key and certificate chain and checks that the key matches the leaf certificate. `OpenSslSigner::sign()` performs SHA-256 ECDSA signing and can return DER or fixed-width raw `r || s`. The iAP2 authentication state machine now rejects a signing failure rather than transmitting an empty signature. Live Bluetooth/USB iAP2 mode accepts explicit `--mfi-cert` and `--mfi-key` paths and refuses to start without them. Identification messages `0x1D00`, `0x1D01`, and `0x1D02` follow the local decompiled DiPlay schema. Authentication message IDs and parameter-0 encoding also match that source. After authentication, the receiver sends the five control subscriptions used by the local DiPlay client. Wi-Fi handoff handling supports CarPlay availability updates and device transport notifications, including the reference's five pre-transport and two post-transport configuration-send limits. Combined `--receiver` mode keeps the Bluetooth iAP2 link active, answers CarPlay availability (`0x4300`) with a source-shaped `CarPlayStartSession` (`0x4301`), starts the AirPlay listener before sending that response, and sends the accessory Wi-Fi config when requested. The start-session payload uses the same AirPlay Ed25519 public key and derived device identifier as `/info` and Bonjour. The Mac hotspot must already be running; pass its reachable IP with `--wifi-ip`, and its SSID/channel/security/passphrase must match the supplied options/environment.

AirPlay pair-setup and pair-verify implement SRP-6a, TLV8, X25519, Ed25519, HKDF, and ChaCha20-Poly1305 from the vendored primitives, following the local DiPlay sequence and labels. The accessory identity and paired controller keys persist in `~/Library/Application Support/iap2-harness/airplay-pairings.bin` with owner-only file permissions; `CARPLAY_PAIRING_FILE` can override the path. After pair-verify, main RTSP control switches to framed ChaCha20-Poly1305 records with incremental buffering and independent counters. Initial AirPlay `SETUP` starts a UDP timing responder and encrypted TCP event endpoint, returns their ports, and advertises `iAPChannel` and `viewAreas`. The RTSP stream-list `SETUP` derives the screen output key and starts an encrypted screen TCP receiver using the source's 128-byte header, frame counter, nonce, and NAL framing. The screen listener now accepts media only from the IP address of the RTSP control peer, matching the local DiPlay session binding. Type-100 `media` audio `SETUP` negotiates data/control UDP ports and starts a peer-restricted receiver that authenticates and decrypts encrypted RTP packets. On macOS, its 16-bit PCM payloads are byte-swapped as in the DiPlay renderer and sent to an AudioQueue output. `/info` and Bonjour advertise the reference audio-enabled feature bitmap and only the PCM `media` format (format bits 4092). AAC, Opus, microphone capture, and live audio playback remain unsupported or unverified. On macOS, decrypted H.264/H.265 access units are passed to VideoToolbox and displayed in an AppKit window. Mac mouse input now maps to 1024×600 touch coordinates and sends the source-format encrypted HID report over the event channel. `/info` includes the CarPlay display and source HID descriptors; `/command` and `/feedback` requests are acknowledged. These AirPlay paths compile but remain unverified with a live iPhone. Knob/media/telephony input and hotspot automation remain incomplete.

## Changes since last status

- Added `--pipe` for raw capture and `--address <iphone>` for a full-duplex PlayPort bridge process transport.
- Added the decompiled-source-based iAP2 identification fields, corrected MFi auth IDs/body parameters, and Wi-Fi configuration exchange.
- Implemented OpenSSL SHA-256 ECDSA signatures in DER and raw `r || s` encodings; live iAP2 runs now load the signer from explicit credential paths. Credential contents were not inspected during this change.
- Added an AirPlay SRP-6a server and wired pair-setup/pair-verify RTSP requests to a pairing state machine based on the local DiPlay implementation. Wireless mode accepts successive RTSP connections while retaining pairing state for the process lifetime; live protocol verification, encrypted RTSP control, and media display are still outstanding.
- Added RTSP screen stream negotiation and a TCP screen receiver that validates frame sizes, decrypts frames with the negotiated data stream key, and converts length-prefixed NAL units to Annex-B. Mac video decoding and display are the next media layer.
- Added initial AirPlay `SETUP` handling that starts local UDP timing and encrypted TCP event channels and returns their negotiated ports. Their framing follows local decompiled source and still needs a real phone capture.
- Added a native macOS VideoToolbox H.264/H.265 decoder and AppKit display window for received screen frames; `--wireless` now runs its RTSP server alongside the window event loop.
- Replaced the Apple TV placeholder `/info` response with the local reference's CarPlay capability/display shape and aligned `run-advertise.sh` identity and feature TXT values. `CARPLAY_BT_MAC`, `CARPLAY_MODEL`, `CARPLAY_MANUFACTURER`, and `CARPLAY_NAME` configure the Bluetooth identity and receiver metadata.
- Added the decompiled touch/knob/media/telephony HID descriptors to `/info`, acknowledged `/command` and `/feedback`, and returned `iAPChannel` and `viewAreas` from initial `SETUP`.
- Added combined `--receiver` startup: it starts Bonjour and the Mac window, keeps the MFi-authenticated Bluetooth link alive, then binds AirPlay before replying to `0x4300` with `0x4301`. The start-session payload includes SSID, passphrase, channel, Wi-Fi address, AirPlay port, the matching identity public key/device ID, and source version. `--wifi-ip` and `CARPLAY_WIFI_PASSPHRASE` describe the already running Mac hotspot.
- Combined receiver mode only accepts CarPlay availability after iAP2 MFi authentication succeeds, and derives the advertised device identifier from the shared AirPlay Ed25519 public key as in the local reference.
- Added the five post-authentication iAP2 subscription messages from the Kotlin reference and handling for `0x4E0D` availability updates and `0x4E0E` device transport notifications, with the reference Wi-Fi configuration resend limits.
- Added the Kotlin-reference main RTSP encrypted control records after pair-verify, including little-endian record lengths, 16 KiB chunks, header authentication, key directions, and partial TCP record buffering. Live phone validation remains outstanding.
- Persisted the AirPlay Ed25519 identity and paired controller public keys to an owner-only local state file so Bonjour identity and iPhone pair-verify trust survive receiver restarts.
- Added Mac mouse-to-touch input using the Kotlin reference's two-slot HID report and encrypted `/command` event messages; the event channel now filters RTSP responses instead of acknowledging them as requests.
- Restricted screen and type-100 audio media sockets to the IP address of the accepted RTSP control peer. Reconstructed the source audio SETUP response and encrypted RTP framing from its smali output; added UDP data/control sockets and AEAD verification/decryption for audio packets.
- Added macOS AudioQueue playback for the source's signed 16-bit PCM packet path. `/info` and Bonjour now advertise the source's audio-enabled feature bitmap with only the 44.1 kHz and lower PCM `media` formats (bitmask 4092); AAC, Opus, microphones, and live phone compatibility remain unverified.
- Rebuilt successfully on macOS after these changes. Live phone negotiation and rendered video have not yet been exercised.
- `scripts/capture-bt.sh` records `btmon` output to `captures/bt-<timestamp>.log`; it requires BlueZ `btmon`. On the Mac prototype, use Apple PacketLogger for Bluetooth traces.
- Corrected `run-advertise.sh` to publish only `_airplay._tcp`. The local DiPlay receiver registers AirPlay and discovers `_carplay-ctrl._tcp`; this harness has no port-7001 listener, so advertising that control service was misleading. The change is source-derived and still needs live iPhone validation.
- Added credential-free `--capture --address PHONE` using the full-duplex PlayPort bridge, so it sends SYN to the phone and logs received bytes without parsing them. Fixed bridge EOF detection. `--pipe` remains available for a true full-duplex shell coprocess; a one-way `bt-bridge | iap2-harness --pipe` pipeline cannot return the SYN to the bridge. Build succeeds. The current macOS Bluetooth panel shows the iPhone as nearby rather than paired, so RFCOMM capture has not yet run.
- Fixed PlayPort `bt-bridge` to wait for its RFCOMM-open callback before forwarding queued stdin bytes, preventing an immediate iAP2 SYN from racing channel setup. The bridge rebuilds successfully. A user-approved pairing scan for the iPhone's previously observed address completed without finding it, issued no confirmation code, and the current paired-device list still does not include the iPhone.
- Rechecked Mac network setup: Internet Sharing is off, the Wi-Fi interface is connected to an existing network, and the Ethernet interface is not available as an Internet Sharing source. No network settings were changed. The receiver needs a reachable Wi-Fi SSID/passphrase and host address before wireless handoff.
