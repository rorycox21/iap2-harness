#pragma once
#include "mfi/signer.hpp"

namespace mfi {

class MockSigner : public Signer {
public:
    std::vector<std::vector<std::uint8_t>> certificateChain() const override;
    std::vector<std::uint8_t> sign(ByteView challenge,
                                   SignatureEncoding enc) const override;
};

}  // namespace mfi
