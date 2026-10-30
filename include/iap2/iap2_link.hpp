#pragma once
#include "iap2/message.hpp"
#include <cstdint>
#include <vector>

namespace iap2 {

// iAP2 link packets carry a CSM frame in their payload. The link packet
// uses additive header and payload checksums, matching the DiPlay reference.
class Iap2LinkEncoder {
public:
    static std::vector<std::uint8_t> encode(const Message& m, std::uint8_t seq = 0x64,
                                           std::uint8_t ack = 0);
    static std::vector<std::uint8_t> acknowledgement(std::uint8_t seq, std::uint8_t ack);
};

class Iap2LinkDecoder {
public:
    std::vector<Message> feed(const std::uint8_t* data, std::size_t len);
    void reset();
    std::uint8_t lastReceivedSequence() const { return last_rx_seq_; }
    std::uint64_t dataPacketCount() const { return data_packet_count_; }
private:
    std::vector<std::uint8_t> buf_;
    std::vector<std::uint8_t> csm_buf_;
    std::uint8_t last_rx_seq_ = 0x63;
    std::uint64_t data_packet_count_ = 0;
};

}  // namespace iap2
