#include "iap2/link_handshake.hpp"
#include <cstdio>

namespace iap2 {

// IAP2_MARKER from Iap2LinkEngine: {-1, 85, 2, 0, -18, 0x10} = FF 55 02 00 EE 10
static const std::uint8_t kMarker[] = {0xFF, 0x55, 0x02, 0x00, 0xEE, 0x10};

// Matches Iap2LinkEngine.Companion.checksum(bytes, count):
//   sum bytes, return (-sum) & 0xFF.
static std::uint8_t sum_checksum(const std::uint8_t* p, std::size_t n) {
    std::uint16_t s = 0;
    for (std::size_t i = 0; i < n; ++i) s += p[i];
    return static_cast<std::uint8_t>((0x100 - (s & 0xFF)) & 0xFF);
}

LinkHandshake::LinkHandshake(transport::Transport& t, log::ILogger& log)
    : t_(t), log_(log) {}

bool LinkHandshake::sendMarker() {
    const int w = t_.write(kMarker, sizeof kMarker);
    if (w != static_cast<int>(sizeof kMarker)) {
        log_.warn("marker write failed");
        return false;
    }
    log_.info("marker sent");
    return true;
}

// 19-byte payload per decompiled Iap2LinkEngine.SynchronizationPayload.decode:
//   bytes[0]   = version (skipped by decode, always 1)
//   bytes[1]   = maxOutgoing (u8)
//   bytes[2-3] = maxLength (u16 BE)
//   bytes[4-5] = retransmissionTimeoutMillis (u16 BE)
//   bytes[6-7] = acknowledgementTimeoutMillis (u16 BE)
//   bytes[8]   = maxRetransmissions (u8)
//   bytes[9]   = maxAcknowledgements (u8)
//   bytes[10+] = N × SessionDescriptor {id, type, version} (3 bytes each)
std::vector<std::uint8_t> LinkHandshake::buildSynPayload(const SynParams& p) const {
    std::vector<std::uint8_t> v;
    v.push_back(p.version);
    v.push_back(p.maxOutgoing);
    v.push_back(static_cast<std::uint8_t>(p.maxLength >> 8));
    v.push_back(static_cast<std::uint8_t>(p.maxLength & 0xFF));
    v.push_back(static_cast<std::uint8_t>(p.retransmissionTimeoutMs >> 8));
    v.push_back(static_cast<std::uint8_t>(p.retransmissionTimeoutMs & 0xFF));
    v.push_back(static_cast<std::uint8_t>(p.acknowledgementTimeoutMs >> 8));
    v.push_back(static_cast<std::uint8_t>(p.acknowledgementTimeoutMs & 0xFF));
    v.push_back(p.maxRetransmissions);
    v.push_back(p.maxAcknowledgements);
    for (const auto& s : p.sessions) {
        v.push_back(s.id);
        v.push_back(s.type);
        v.push_back(s.version);
    }
    return v;
}

// Matches Iap2LinkEngine.writePacket exactly:
//   length field = payload.size() + 10  (9 header + payload + 1 checksum byte)
//   header: FF 5A len_hi len_lo control seq ack session crc8
//   payload bytes
//   payload checksum: 1 byte, (-sum) & 0xFF
bool LinkHandshake::sendSyn(const SynParams& p) {
    auto payload = buildSynPayload(p);

    const std::uint16_t total = static_cast<std::uint16_t>(9 + payload.size() + 1);
    std::vector<std::uint8_t> syn;
    syn.reserve(total);
    syn.push_back(0xFF);
    syn.push_back(0x5A);
    syn.push_back(static_cast<std::uint8_t>(total >> 8));
    syn.push_back(static_cast<std::uint8_t>(total & 0xFF));
    syn.push_back(0x80);           // control: CONTROL_SYNCHRONIZE
    syn.push_back(0x63);           // seq: INITIAL_SEQUENCE (99)
    syn.push_back(0x00);           // ack: lastReceivedInOrder (0 initially)
    syn.push_back(0x00);           // session: 0 (link control)
    syn.push_back(sum_checksum(syn.data(), 8));
    syn.insert(syn.end(), payload.begin(), payload.end());
    syn.push_back(sum_checksum(payload.data(), payload.size()));

    std::string synhex;
    for (auto b : syn) {
        char hb[4]; std::snprintf(hb, sizeof hb, "%02x ", b); synhex += hb;
    }
    log_.debug("tx SYN hex: " + synhex);
    log_.debug("tx SYN: " + std::to_string(syn.size()) + " bytes, length=" +
               std::to_string(total) + ", payload=" + std::to_string(payload.size()));
    const int w = t_.write(syn.data(), syn.size());
    return w == static_cast<int>(syn.size());
}

bool LinkHandshake::waitForPeerSyn() {
    std::uint8_t buf[4096];
    const int n = t_.read(buf, sizeof buf, 5000);
    if (n <= 0) {
        log_.warn("no SYN response from phone");
        return false;
    }

    std::string hex;
    for (int i = 0; i < n && i < 64; ++i) {
        char b[4];
        std::snprintf(b, sizeof b, "%02x ", buf[i]);
        hex += b;
    }
    log_.debug("rx " + std::to_string(n) + " bytes: " + hex);

    if (n < 9 || buf[0] != 0xFF || buf[1] != 0x5A) {
        log_.warn("SYN response: bad SOP");
        return false;
    }
    const std::uint16_t len = (static_cast<std::uint16_t>(buf[2]) << 8) | buf[3];
    if (n < len) {
        log_.warn("SYN response: short packet (" + std::to_string(n) +
                  " < " + std::to_string(len) + ")");
        return false;
    }
    if ((buf[4] & 0x80) == 0) {
        log_.warn("SYN response: not a SYN (control=0x" +
                  std::to_string(buf[4]) + ")");
        return false;
    }
    // Header checksum: sum of all 9 header bytes must be 0 mod 256.
    std::uint16_t hsum = 0;
    for (int i = 0; i < 9; ++i) hsum += buf[i];
    if ((hsum & 0xFF) != 0) {
        log_.warn("SYN response: header checksum invalid");
        return false;
    }

    log_.info("received peer SYN, sending ACK");

    // ACK: 9-byte header, no payload, no payload checksum. Length field = 9.
    // control = CONTROL_ACKNOWLEDGEMENT (0x40).
    const std::uint8_t peer_seq = buf[5];
    std::vector<std::uint8_t> ack = {
        0xFF, 0x5A, 0x00, 0x09,
        0x40,
        0x64,                                             // next sequence (99+1)
        static_cast<std::uint8_t>((peer_seq + 1) & 0xFF), // ack peer_seq+1
        0x00
    };
    ack.push_back(sum_checksum(ack.data(), 8));

    const int w = t_.write(ack.data(), ack.size());
    if (w != static_cast<int>(ack.size())) {
        log_.warn("ACK write failed");
        return false;
    }
    log_.info("link established");
    return true;
}

bool LinkHandshake::sendSynOnly() {
    if (!sendMarker()) return false;
    SynParams p;
    return sendSyn(p);
}

bool LinkHandshake::perform() {
    if (!sendMarker()) return false;
    SynParams p;
    if (!sendSyn(p)) return false;
    return waitForPeerSyn();
}

}  // namespace iap2
