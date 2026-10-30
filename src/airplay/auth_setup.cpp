#include "airplay/auth_setup.hpp"
#include "airplay_crypto.h"
#include <openssl/evp.h>
#include <algorithm>
#include <memory>
#include <string>

namespace airplay {
namespace {
using Bytes = std::vector<std::uint8_t>;
Bytes digest(const Bytes& data, const EVP_MD* md) {
    Bytes out(EVP_MAX_MD_SIZE); unsigned size = 0;
    if (EVP_Digest(data.data(), data.size(), out.data(), &size, md, nullptr) != 1) return {};
    out.resize(size); return out;
}
Bytes derive(const char* label, const Bytes& secret) {
    const std::string text(label);
    Bytes input(text.begin(),text.end()); input.insert(input.end(),secret.begin(),secret.end());
    auto out = digest(input,EVP_sha1());
    if (out.size() != 20) return {};
    out.resize(16); return out;
}
void size32(Bytes& out, std::size_t n) {
    for (int shift=24; shift>=0; shift-=8) out.push_back(static_cast<std::uint8_t>(n >> shift));
}
}
std::optional<Bytes> mfiAuthSetup(const Bytes& request, mfi::Signer& signer) {
    if (request.size()!=33 || request[0]!=1) return std::nullopt;
    try {
        const Bytes peer(request.begin()+1,request.end());
        const auto pair=fxchain::airplay::x25519Generate();
        const auto shared=fxchain::airplay::x25519SharedSecret(pair.priv,peer);
        if (shared.size()!=32 || std::all_of(shared.begin(),shared.end(),[](auto b){return b==0;})) return std::nullopt;
        const auto key=derive("AES-KEY",shared), iv=derive("AES-IV",shared);
        if (key.size()!=16 || iv.size()!=16) return std::nullopt;
        Bytes publicKeys=pair.pub; publicKeys.insert(publicKeys.end(),peer.begin(),peer.end());
        const auto hash=digest(publicKeys,EVP_sha256());
        const auto signature=signer.sign(mfi::ByteView(hash),mfi::SignatureEncoding::RawRS);
        const auto chain=signer.certificateChain();
        Bytes certificate;
        for (const auto& part:chain) certificate.insert(certificate.end(),part.begin(),part.end());
        if (signature.size()!=64 || certificate.empty() || certificate.size()>65525) return std::nullopt;
        using Ctx=std::unique_ptr<EVP_CIPHER_CTX,decltype(&EVP_CIPHER_CTX_free)>;
        Ctx ctx(EVP_CIPHER_CTX_new(),EVP_CIPHER_CTX_free);
        Bytes encrypted(signature.size()+16); int count=0,tail=0;
        if (!ctx || EVP_EncryptInit_ex(ctx.get(),EVP_aes_128_ctr(),nullptr,key.data(),iv.data())!=1 ||
            EVP_EncryptUpdate(ctx.get(),encrypted.data(),&count,signature.data(),signature.size())!=1 ||
            EVP_EncryptFinal_ex(ctx.get(),encrypted.data()+count,&tail)!=1) return std::nullopt;
        encrypted.resize(count+tail);
        Bytes response=pair.pub;
        size32(response,certificate.size()); response.insert(response.end(),certificate.begin(),certificate.end());
        size32(response,encrypted.size()); response.insert(response.end(),encrypted.begin(),encrypted.end());
        return response;
    } catch (...) { return std::nullopt; }
}
}
