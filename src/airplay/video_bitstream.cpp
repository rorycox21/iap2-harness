#include "airplay/video_bitstream.hpp"
#include <cstdint>
namespace airplay {
using Bytes = fxchain::airplay::Bytes;
Bytes lengthPrefixedToAnnexB(const Bytes& input, unsigned length_bytes) {
    if (length_bytes < 1 || length_bytes > 4) return {};
    Bytes out;
    std::size_t offset = 0;
    while (offset < input.size()) {
        if (input.size() - offset < length_bytes) return {};
        std::uint32_t size = 0;
        for (unsigned i = 0; i < length_bytes; ++i) size = (size << 8) | input[offset++];
        if (!size || size > input.size() - offset) return {};
        out.insert(out.end(), {0, 0, 0, 1});
        out.insert(out.end(), input.begin() + offset, input.begin() + offset + size);
        offset += size;
    }
    return out;
}
Bytes annexBToLengthPrefixed(const Bytes& input) {
    auto find = [&input](std::size_t from, std::size_t& prefix) {
        for (std::size_t i = from; i + 3 <= input.size(); ++i) {
            if (input[i] || input[i + 1]) continue;
            if (input[i + 2] == 1) { prefix = 3; return i; }
            if (i + 4 <= input.size() && input[i + 2] == 0 && input[i + 3] == 1) { prefix = 4; return i; }
        }
        return input.size();
    };
    std::size_t prefix = 0, pos = find(0, prefix);
    if (pos != 0) return {};
    Bytes out;
    while (pos < input.size()) {
        const auto start = pos + prefix;
        std::size_t next_prefix = 0;
        const auto next = find(start, next_prefix);
        if (next <= start || next - start > UINT32_MAX) return {};
        const auto size = static_cast<std::uint32_t>(next - start);
        out.insert(out.end(), {static_cast<std::uint8_t>(size >> 24), static_cast<std::uint8_t>(size >> 16),
                              static_cast<std::uint8_t>(size >> 8), static_cast<std::uint8_t>(size)});
        out.insert(out.end(), input.begin() + start, input.begin() + next);
        pos = next; prefix = next_prefix;
    }
    return out;
}
}
