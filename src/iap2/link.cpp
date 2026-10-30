#include "iap2/link.hpp"

namespace iap2 {

static constexpr std::uint8_t kSof = 0xFF;

std::vector<std::uint8_t> LinkEncoder::encode(const Message& m) {
    auto body = m.encode();
    if (body.size() > 255) return {};
    std::vector<std::uint8_t> frame;
    frame.reserve(3 + body.size());
    frame.push_back(kSof);
    frame.push_back(static_cast<std::uint8_t>(body.size()));
    std::uint8_t chk = 0xFF;
    chk ^= static_cast<std::uint8_t>(body.size());
    for (auto b : body) chk ^= b;
    frame.push_back(chk);
    frame.insert(frame.end(), body.begin(), body.end());
    return frame;
}

void LinkDecoder::reset() {
    buf_.clear();
    scanned_ = 0;
}

bool LinkDecoder::try_parse_front(std::vector<std::uint8_t>& out_msg_bytes) {
    if (buf_.size() < 3) return false;
    if (buf_[0] != kSof) return false;
    const std::size_t len = buf_[1];
    if (buf_.size() < 3 + len) return false;
    const std::uint8_t chk_expected = buf_[2];
    std::uint8_t chk = 0xFF;
    chk ^= static_cast<std::uint8_t>(len);
    for (std::size_t i = 0; i < len; ++i) chk ^= buf_[3 + i];
    if (chk != chk_expected) return false;
    out_msg_bytes.assign(buf_.begin() + 3, buf_.begin() + 3 + len);
    buf_.erase(buf_.begin(), buf_.begin() + 3 + len);
    scanned_ = 0;
    return true;
}

std::vector<Message> LinkDecoder::feed(const std::uint8_t* data, std::size_t len) {
    std::vector<Message> out;
    buf_.insert(buf_.end(), data, data + len);

    // Search for the next SOF. Bytes before it are garbage and get dropped.
    while (true) {
        // Advance to next SOF if the front isn't one.
        while (!buf_.empty() && buf_[0] != kSof) buf_.erase(buf_.begin());
        if (buf_.size() < 3) break;
        const std::size_t len_field = buf_[1];
        if (buf_.size() < 3 + len_field) break;

        std::vector<std::uint8_t> msg_bytes;
        if (!try_parse_front(msg_bytes)) {
            // Checksum mismatch: drop the SOF and resync.
            buf_.erase(buf_.begin());
            continue;
        }
        Message m;
        std::size_t consumed = Message::decode(msg_bytes.data(), msg_bytes.size(), 0, m);
        if (consumed == 0 || consumed != msg_bytes.size()) {
            // Inner message malformed; drop and resync.
            buf_.erase(buf_.begin());
            continue;
        }
        out.push_back(std::move(m));
    }
    return out;
}

}  // namespace iap2
