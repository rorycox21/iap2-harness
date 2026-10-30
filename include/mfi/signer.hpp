#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mfi {

enum class SignatureEncoding {
    Der,
    RawRS,
};

// Non-owning view over bytes. std::span is C++20; this keeps the interface
// usable on C++17 toolchains (QNX SDP 7.1, older clang, etc.).
struct ByteView {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    ByteView() = default;
    ByteView(const std::uint8_t* d, std::size_t n) : data(d), size(n) {}
    template <typename Container>
    explicit ByteView(const Container& c) : data(c.data()), size(c.size()) {}
};

class Signer {
public:
    virtual ~Signer() = default;
    virtual std::vector<std::vector<std::uint8_t>> certificateChain() const = 0;
    virtual std::vector<std::uint8_t> sign(ByteView challenge,
                                           SignatureEncoding enc) const = 0;
};

}  // namespace mfi
