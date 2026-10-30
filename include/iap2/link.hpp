#pragma once
#include "iap2/message.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace iap2 {

// iAP2 link-layer framing.
//
// On-wire layout used by this project:
//   offset  size  field
//   0       1     SOF (0xFF)
//   1       1     length (of the message body, 0..255)
//   2       1     checksum: XOR of length, body bytes, and an initial 0xFF
//   3       N     message body (the iap2::Message payload, i.e. the 4-byte
//                 msg-id + length header plus its own payload)
//
// Total frame size = 3 + length.
//
// UNVERIFIED: libimobiledevice and Apple's spec disagree on the checksum
// seed and whether the length includes the 3-byte header. This version
// follows libimobiledevice's `idevice_link.c`: SOF = 0xFF, one length byte,
// one checksum byte computed over the length and body starting from 0xFF.
// If the phone stalls on first contact, this is the first thing to change.
class LinkEncoder {
public:
    // Encodes one iap2::Message into a link-layer frame.
    // Returns empty vector if the encoded message exceeds 255 bytes.
    static std::vector<std::uint8_t> encode(const Message& m);
};

class LinkDecoder {
public:
    // Feed raw bytes from the transport. Returns zero or more complete
    // frames whose payloads have been decoded into Messages. Callers
    // should feed the returned messages to AuthSession.
    // Malformed input is skipped; the decoder resyncs on the next SOF.
    std::vector<Message> feed(const std::uint8_t* data, std::size_t len);

    // Reset internal buffer (e.g. after a transport error).
    void reset();

    // Number of bytes currently buffered (for diagnostics).
    std::size_t buffered() const { return buf_.size(); }

private:
    std::vector<std::uint8_t> buf_;
    std::size_t scanned_ = 0;  // how much of buf_ has been searched for SOF

    // Try to parse one frame from buf_ starting at `pos`. If a full frame
    // is available, append its decoded message to `out`, erase the bytes
    // from buf_, and return true. If not enough bytes are available,
    // return false without modifying buf_.
    bool try_parse_front(std::vector<std::uint8_t>& out_msg_bytes);
};

}  // namespace iap2
