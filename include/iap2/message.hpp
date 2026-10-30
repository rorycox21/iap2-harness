#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace iap2 {

enum class MsgId : std::uint16_t {
    RequestAuthenticationCert     = 0xAA00,
    AuthenticationCertificate     = 0xAA01,
    RequestAuthChallengeResponse  = 0xAA02,
    AuthenticationResponse        = 0xAA03,
    AuthenticationFailed          = 0xAA04,
    AuthenticationSucceeded       = 0xAA05,
    AuthInformation               = 0xAA06,
};

const char* msgName(MsgId id);

struct Message {
    MsgId id = MsgId::AuthInformation;
    std::vector<std::uint8_t> payload;
    std::vector<std::uint8_t> encode() const;
    static std::size_t decode(const std::uint8_t* bytes, std::size_t len,
                              std::size_t offset, Message& out);
};

}  // namespace iap2
