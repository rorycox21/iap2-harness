#include "mfi/openssl_signer.hpp"

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/pkcs7.h>
#include <openssl/x509.h>
#include <cstdio>
#include <memory>

namespace mfi {

struct OpenSslSigner::Impl {
    EVP_PKEY* key = nullptr;
    STACK_OF(X509)* chain = nullptr;
};

OpenSslSigner::OpenSslSigner() : d_(std::make_unique<Impl>()) {}

OpenSslSigner::~OpenSslSigner() {
    if (d_->key) EVP_PKEY_free(d_->key);
    if (d_->chain) sk_X509_pop_free(d_->chain, X509_free);
}

std::unique_ptr<OpenSslSigner> OpenSslSigner::load(const std::string& cert_path,
                                                   const std::string& key_path,
                                                   std::string& error) {
    auto s = std::unique_ptr<OpenSslSigner>(new OpenSslSigner());

    // 1. Load the private key (PKCS#8, unencrypted, DER).
    {
        BIO* bio = BIO_new_file(key_path.c_str(), "rb");
        if (!bio) { error = "cannot open key file"; return nullptr; }
        PKCS8_PRIV_KEY_INFO* p8 = nullptr;
        p8 = d2i_PKCS8_PRIV_KEY_INFO_bio(bio, nullptr);
        BIO_free(bio);
        if (!p8) { error = "cannot parse PKCS#8 key"; return nullptr; }
        s->d_->key = EVP_PKCS82PKEY(p8);
        PKCS8_PRIV_KEY_INFO_free(p8);
        if (!s->d_->key) { error = "cannot convert PKCS#8 to EVP_PKEY"; return nullptr; }
    }

    // 2. Load the certificate chain (PKCS#7, DER).
    {
        BIO* bio = BIO_new_file(cert_path.c_str(), "rb");
        if (!bio) { error = "cannot open cert file"; return nullptr; }
        PKCS7* p7 = d2i_PKCS7_bio(bio, nullptr);
        BIO_free(bio);
        if (!p7) { error = "cannot parse PKCS#7"; return nullptr; }
        s->d_->chain = PKCS7_get0_signers(p7, nullptr, 0);
        PKCS7_free(p7);
        if (!s->d_->chain || sk_X509_num(s->d_->chain) == 0) {
            error = "no certificates in PKCS#7";
            return nullptr;
        }
    }

    // 3. Check the private key matches the leaf certificate.
    {
        X509* leaf = sk_X509_value(s->d_->chain, 0);
        EVP_PKEY* pub = X509_get_pubkey(leaf);
        if (!pub) { error = "cannot extract leaf public key"; return nullptr; }
        int ok = EVP_PKEY_eq(s->d_->key, pub);
        EVP_PKEY_free(pub);
        if (ok != 1) { error = "private key does not match certificate"; return nullptr; }
    }

    return s;
}

std::vector<std::vector<std::uint8_t>> OpenSslSigner::certificateChain() const {
    std::vector<std::vector<std::uint8_t>> out;
    if (!d_->chain) return out;
    for (int i = 0; i < sk_X509_num(d_->chain); ++i) {
        X509* cert = sk_X509_value(d_->chain, i);
        unsigned char* der = nullptr;
        int len = i2d_X509(cert, &der);
        if (len > 0) out.emplace_back(der, der + len);
        OPENSSL_free(der);
    }
    return out;
}

std::vector<std::uint8_t> OpenSslSigner::sign(ByteView challenge,
                                              SignatureEncoding enc) const {
    if (!d_->key || (challenge.size != 0 && challenge.data == nullptr)) return {};

    using MdCtx = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
    MdCtx ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!ctx || EVP_DigestSignInit(ctx.get(), nullptr, EVP_sha256(), nullptr, d_->key) != 1)
        return {};

    std::size_t signature_size = 0;
    if (EVP_DigestSign(ctx.get(), nullptr, &signature_size,
                       challenge.data, challenge.size) != 1 || signature_size == 0)
        return {};

    std::vector<std::uint8_t> signature(signature_size);
    if (EVP_DigestSign(ctx.get(), signature.data(), &signature_size,
                       challenge.data, challenge.size) != 1)
        return {};
    signature.resize(signature_size);

    if (enc == SignatureEncoding::Der) return signature;
    if (enc != SignatureEncoding::RawRS || EVP_PKEY_get_bits(d_->key) != 256)
        return {};

    const unsigned char* cursor = signature.data();
    using EcdsaSig = std::unique_ptr<ECDSA_SIG, decltype(&ECDSA_SIG_free)>;
    EcdsaSig parsed(d2i_ECDSA_SIG(nullptr, &cursor,
                                  static_cast<long>(signature.size())), ECDSA_SIG_free);
    if (!parsed || cursor != signature.data() + signature.size()) return {};

    const BIGNUM* r = nullptr;
    const BIGNUM* s = nullptr;
    ECDSA_SIG_get0(parsed.get(), &r, &s);
    std::vector<std::uint8_t> raw(64);
    if (!r || !s || BN_bn2binpad(r, raw.data(), 32) != 32 ||
        BN_bn2binpad(s, raw.data() + 32, 32) != 32)
        return {};
    return raw;
}

}  // namespace mfi
