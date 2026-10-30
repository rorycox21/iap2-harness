#include "iap2/message.hpp"
#include <cstring>

namespace iap2 {

const char* msgName(MsgId id) {
    switch (id) {
        case MsgId::AuthInformation:              return "AuthInformation";
        case MsgId::RequestAuthenticationCert:    return "RequestAuthenticationCertificate";
        case MsgId::AuthenticationCertificate:    return "AuthenticationCertificate";
        case MsgId::RequestAuthChallengeResponse: return "RequestAuthenticationChallengeResponse";
        case MsgId::AuthenticationResponse:       return "AuthenticationResponse";
        case MsgId::AuthenticationSucceeded:      return "AuthenticationSucceeded";
        case MsgId::AuthenticationFailed:         return "AuthenticationFailed";
    }
    return "Unknown";
}

static void put16(std::vector<std::uint8_t>& b, std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>(v >> 8));
    b.push_back(static_cast<std::uint8_t>(v & 0xFF));
}
static std::uint16_t rd16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

std::vector<std::uint8_t> Message::encode() const {
    std::vector<std::uint8_t> out;
    out.reserve(4 + payload.size());
    put16(out, static_cast<std::uint16_t>(id));
    put16(out, static_cast<std::uint16_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::size_t Message::decode(const std::uint8_t* bytes, std::size_t len,
                            std::size_t offset, Message& out) {
    if (len - offset < 4) return 0;
    const std::uint16_t id   = rd16(bytes + offset);
    const std::uint16_t plen = rd16(bytes + offset + 2);
    if (len - offset - 4 < plen) return 0;
    out.id = static_cast<MsgId>(id);
    out.payload.assign(bytes + offset + 4, bytes + offset + 4 + plen);
    return 4 + plen;
}

}  // namespace iap2
