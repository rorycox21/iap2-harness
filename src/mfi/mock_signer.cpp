#include "mfi/mock_signer.hpp"
#include <openssl/evp.h>

namespace mfi {

std::vector<std::vector<std::uint8_t>> MockSigner::certificateChain() const {
    static const std::uint8_t kPlaceholder[] = {
        0x30, 0x0a, 0x02, 0x01, 0x00, 0x05, 0x00, 0x04, 0x02, 0x00, 0x00
    };
    return {std::vector<std::uint8_t>(std::begin(kPlaceholder), std::end(kPlaceholder))};
}

std::vector<std::uint8_t> MockSigner::sign(ByteView challenge,
                                           SignatureEncoding enc) const {
    std::uint8_t digest[32];
    unsigned int dlen = 0;
    EVP_Digest(challenge.data, challenge.size, digest, &dlen, EVP_sha256(), nullptr);
    if (enc == SignatureEncoding::RawRS) {
        std::vector<std::uint8_t> out;
        out.reserve(64);
        out.insert(out.end(), digest, digest + 32);
        out.insert(out.end(), digest, digest + 32);
        return out;
    }
    return std::vector<std::uint8_t>(digest, digest + 32);
}

}  // namespace mfi
