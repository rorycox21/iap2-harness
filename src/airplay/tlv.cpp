#include "airplay/tlv.hpp"
#include <stdexcept>

namespace airplay {

std::map<std::uint8_t, std::vector<std::uint8_t>>
Tlv8::decode(const std::vector<std::uint8_t>& buf) {
    std::map<std::uint8_t, std::vector<std::uint8_t>> out;
    std::size_t i = 0;
    while (i + 2 <= buf.size()) {
        std::uint8_t tag = buf[i];
        std::uint8_t len = buf[i + 1];
        i += 2;
        if (i + len > buf.size()) break;  // truncated
        auto& v = out[tag];
        v.insert(v.end(), buf.begin() + i, buf.begin() + i + len);
        i += len;
    }
    return out;
}

void Tlv8::append(std::vector<std::uint8_t>& out,
                  std::uint8_t tag,
                  const std::vector<std::uint8_t>& value) {
    std::size_t off = 0;
    if (value.empty()) {
        out.push_back(tag); out.push_back(0);
        return;
    }
    while (off < value.size()) {
        std::size_t chunk = std::min<std::size_t>(255, value.size() - off);
        out.push_back(tag);
        out.push_back(static_cast<std::uint8_t>(chunk));
        out.insert(out.end(), value.begin() + off, value.begin() + off + chunk);
        off += chunk;
    }
}

}  // namespace airplay
