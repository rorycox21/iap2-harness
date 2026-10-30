#pragma once
#include <cstdint>
#include <map>
#include <vector>

namespace airplay {

// Minimal TLV8 codec for AirPlay/HAP pairing messages.
// Format: tag (1 byte) + length (1 byte, 0..255) + value.
// Values > 255 bytes are fragmented into multiple TLVs with the same tag.
// See GNOME gnome-network-displays airplay-pairing.c for reference.
class Tlv8 {
public:
    // Parse a buffer into a map of tag -> concatenated value.
    static std::map<std::uint8_t, std::vector<std::uint8_t>>
    decode(const std::vector<std::uint8_t>& buf);

    // Encode a single tag/value pair (fragments if > 255 bytes).
    static void append(std::vector<std::uint8_t>& out,
                       std::uint8_t tag,
                       const std::vector<std::uint8_t>& value);

    // Common tags (from HAP/AirPlay pairing spec).
    static constexpr std::uint8_t kTagMethod = 0x00;
    static constexpr std::uint8_t kTagSeqNo = 0x06;
    static constexpr std::uint8_t kTagPublicKey = 0x03;
    static constexpr std::uint8_t kTagProof = 0x04;
    static constexpr std::uint8_t kTagEncryptedData = 0x05;
    static constexpr std::uint8_t kTagSalt = 0x02;
};

}  // namespace airplay
