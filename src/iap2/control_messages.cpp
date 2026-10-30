// Encodes the iAP2 subscription messages that the local DiPlay client sends
// after authentication. The source schema is known; live interoperability is
// still unverified.
#include "iap2/control_messages.hpp"

#include <cstdint>
#include <initializer_list>
#include <utility>

namespace iap2 {
namespace {

void put16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

void parameter(std::vector<std::uint8_t>& out, std::uint16_t id,
               const std::vector<std::uint8_t>& value) {
    put16(out, static_cast<std::uint16_t>(value.size() + 4));
    put16(out, id);
    out.insert(out.end(), value.begin(), value.end());
}

std::vector<std::uint8_t> voidFields(std::initializer_list<std::uint16_t> ids) {
    std::vector<std::uint8_t> out;
    for (const auto id : ids) parameter(out, id, {});
    return out;
}

Message message(std::uint16_t id, std::vector<std::uint8_t> payload) {
    Message result;
    result.id = static_cast<MsgId>(id);
    result.payload = std::move(payload);
    return result;
}

}  // namespace

std::vector<Message> controlSubscriptions() {
    std::vector<Message> out;
    out.reserve(5);

    // StartNowPlayingUpdates (0x5000): subscribe to metadata and playback state.
    {
        std::vector<std::uint8_t> payload;
        parameter(payload, 0, voidFields({1, 4, 6, 12, 26}));
        parameter(payload, 1, voidFields({0, 1, 7}));
        out.push_back(message(0x5000, std::move(payload)));
    }

    // StartRouteGuidanceUpdates (0x5200): route type 42, plus fields 1 and 2.
    {
        std::vector<std::uint8_t> payload;
        parameter(payload, 0, {0, 42});
        parameter(payload, 1, {});
        parameter(payload, 2, {});
        out.push_back(message(0x5200, std::move(payload)));
    }

    out.push_back(message(0xAE00, voidFields({4, 5, 6})));
    out.push_back(message(0x4157, voidFields({0, 4, 5})));
    out.push_back(message(0x4154, voidFields({0, 1, 2, 3, 4, 11})));
    return out;
}

}  // namespace iap2
