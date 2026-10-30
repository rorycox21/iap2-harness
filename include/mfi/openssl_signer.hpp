#pragma once
#include "mfi/signer.hpp"
#include <memory>
#include <string>

namespace mfi {

// Loads a PKCS#8 EC P-256 private key and a PKCS#7 certificate chain from
// disk, checks that the key matches the leaf certificate, and exposes the
// Signer interface with SHA-256 ECDSA signing.
class OpenSslSigner : public Signer {
public:
    // cert_path: DER PKCS#7 (.p7b)
    // key_path:  DER PKCS#8 unencrypted EC P-256 (.pk8)
    // Returns nullptr if either file fails to load or the key does not
    // match the certificate.
    static std::unique_ptr<OpenSslSigner> load(const std::string& cert_path,
                                               const std::string& key_path,
                                               std::string& error);

    ~OpenSslSigner() override;

    std::vector<std::vector<std::uint8_t>> certificateChain() const override;

    // Signs the challenge with SHA-256 ECDSA and returns DER or fixed-width
    // 32-byte r || 32-byte s. Returns an empty vector if signing fails.
    std::vector<std::uint8_t> sign(ByteView challenge,
                                   SignatureEncoding enc) const override;

private:
    OpenSslSigner();
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace mfi
