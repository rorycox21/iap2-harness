#include "iap2/iap2_link.hpp"

namespace iap2 {
namespace {
std::uint8_t checksum(const std::uint8_t* p, std::size_t n) {
    std::uint16_t sum = 0;
    for (std::size_t i = 0; i < n; ++i) sum += p[i];
    return static_cast<std::uint8_t>((0x100 - (sum & 0xFF)) & 0xFF);
}
void put16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}
std::uint16_t get16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
}
std::vector<std::uint8_t> csmFrame(const Message& m) {
    if (m.payload.size() > 65529) return {};
    const auto length = static_cast<std::uint16_t>(m.payload.size() + 6);
    std::vector<std::uint8_t> out{0xFF, 0x5A};
    put16(out, length);
    put16(out, static_cast<std::uint16_t>(m.id));
    out.insert(out.end(), m.payload.begin(), m.payload.end());
    return out;
}
std::vector<std::uint8_t> linkPacket(const std::uint8_t* payload, std::size_t payload_len,
                                     std::uint8_t seq, std::uint8_t ack,
                                     std::uint8_t control) {
    const std::uint16_t length = static_cast<std::uint16_t>(payload ? payload_len + 10 : 9);
    std::vector<std::uint8_t> out{0xFF, 0x5A};
    put16(out, length);
    out.push_back(control);
    out.push_back(seq);
    out.push_back(ack);
    out.push_back(0x00);  // link control session
    out.push_back(checksum(out.data(), out.size()));
    if (payload) {
        out.insert(out.end(), payload, payload + payload_len);
        out.push_back(checksum(payload, payload_len));
    }
    return out;
}
}  // namespace

std::vector<std::uint8_t> Iap2LinkEncoder::encode(const Message& m, std::uint8_t seq,
                                                  std::uint8_t ack) {
    const auto csm = csmFrame(m);
    if (csm.empty()) return {};
    return linkPacket(csm.data(), csm.size(), seq, ack, 0x40);
}

std::vector<std::uint8_t> Iap2LinkEncoder::acknowledgement(std::uint8_t seq,
                                                            std::uint8_t ack) {
    return linkPacket(nullptr, 0, seq, ack, 0x40);
}

void Iap2LinkDecoder::reset() {
    buf_.clear();
    csm_buf_.clear();
    last_rx_seq_ = 0x63;
    data_packet_count_ = 0;
}

std::vector<Message> Iap2LinkDecoder::feed(const std::uint8_t* data, std::size_t len) {
    std::vector<Message> out;
    if (len) buf_.insert(buf_.end(), data, data + len);
    while (true) {
        while (buf_.size() >= 2 && !(buf_[0] == 0xFF && buf_[1] == 0x5A))
            buf_.erase(buf_.begin());
        if (buf_.size() < 9) break;
        const std::size_t packet_len = get16(buf_.data() + 2);
        if (packet_len < 9) { buf_.erase(buf_.begin()); continue; }
        if (buf_.size() < packet_len) break;
        if (checksum(buf_.data(), 8) != buf_[8]) { buf_.erase(buf_.begin()); continue; }

        const std::uint8_t control = buf_[4];
        if (control & 0x80) {
            buf_.erase(buf_.begin(), buf_.begin() + packet_len);
            continue;
        }
        if ((control & 0x40) == 0 || packet_len < 10) {
            buf_.erase(buf_.begin(), buf_.begin() + packet_len);
            continue;
        }
        const std::size_t payload_len = packet_len - 10;
        const std::uint8_t* payload = buf_.data() + 9;
        if (checksum(payload, payload_len) != buf_[packet_len - 1]) {
            buf_.erase(buf_.begin());
            continue;
        }
        last_rx_seq_ = buf_[5];
        ++data_packet_count_;
        csm_buf_.insert(csm_buf_.end(), payload, payload + payload_len);
        buf_.erase(buf_.begin(), buf_.begin() + packet_len);

        while (true) {
            while (csm_buf_.size() >= 2 && !(csm_buf_[0] == 0xFF && csm_buf_[1] == 0x5A))
                csm_buf_.erase(csm_buf_.begin());
            if (csm_buf_.size() < 6) break;
            const std::size_t csm_len = get16(csm_buf_.data() + 2);
            if (csm_len < 6) { csm_buf_.erase(csm_buf_.begin()); continue; }
            if (csm_buf_.size() < csm_len) break;
            Message m;
            m.id = static_cast<MsgId>(get16(csm_buf_.data() + 4));
            m.payload.assign(csm_buf_.begin() + 6, csm_buf_.begin() + csm_len);
            out.push_back(std::move(m));
            csm_buf_.erase(csm_buf_.begin(), csm_buf_.begin() + csm_len);
        }
    }
    return out;
}

}  // namespace iap2
