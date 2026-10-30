#include "iap2/auth_session.hpp"
#include "mfi/mock_signer.hpp"
#include "log/logger.hpp"
#include <cstdio>

static int g_fails = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if(!(c)) { ++g_fails; \
  std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while(0)

static void test_mock_signer() {
    mfi::MockSigner s;
    std::vector<std::uint8_t> challenge = {1, 2, 3, 4, 5};
    auto sig = s.sign(mfi::ByteView(challenge), mfi::SignatureEncoding::RawRS);
    CHECK(sig.size() == 64);
    CHECK(sig[0] == 0x74);
    CHECK(sig[32] == sig[0]);
    auto chain = s.certificateChain();
    CHECK(chain.size() == 1);
    CHECK(!chain[0].empty());
}

static void test_message_roundtrip() {
    iap2::Message m;
    m.id = iap2::MsgId::RequestAuthChallengeResponse;
    m.payload = {0xDE, 0xAD, 0xBE, 0xEF};
    auto wire = m.encode();
    CHECK(wire.size() == 8);
    CHECK(wire[0] == 0xAA && wire[1] == 0x02);
    CHECK(wire[2] == 0x00 && wire[3] == 0x04);
    iap2::Message out;
    std::size_t consumed = iap2::Message::decode(wire.data(), wire.size(), 0, out);
    CHECK(consumed == wire.size());
    CHECK(out.id == m.id);
    CHECK(out.payload == m.payload);
    CHECK(iap2::Message::decode(wire.data(), 3, 0, out) == 0);
    CHECK(iap2::Message::decode(wire.data(), 7, 0, out) == 0);
}

static void test_auth_happy_path() {
    mfi::MockSigner signer;
    log::BufferLogger log;
    iap2::AuthSession s(signer, log);
    iap2::Message req; req.id = iap2::MsgId::RequestAuthenticationCert;
    auto out = s.onMessage(req);
    CHECK(out.size() == 1);
    if (out.size() != 1) return;
    CHECK(out[0].id == iap2::MsgId::AuthenticationCertificate);
    CHECK(!out[0].payload.empty());
    CHECK(s.state() == iap2::AuthState::CertRequested);
    iap2::Message chal;
    chal.id = iap2::MsgId::RequestAuthChallengeResponse;
    chal.payload = {0, 9, 0, 0, 1, 2, 3, 4, 5};
    out = s.onMessage(chal);
    CHECK(out.size() == 1);
    if (out.size() != 1) return;
    CHECK(out[0].id == iap2::MsgId::AuthenticationResponse);
    CHECK(!out[0].payload.empty());
    CHECK(s.state() == iap2::AuthState::ChallengeRequested);
    iap2::Message ok; ok.id = iap2::MsgId::AuthenticationSucceeded;
    out = s.onMessage(ok);
    CHECK(out.empty());
    CHECK(s.state() == iap2::AuthState::Succeeded);
}

static void test_auth_failure() {
    mfi::MockSigner signer;
    log::BufferLogger log;
    iap2::AuthSession s(signer, log);
    iap2::Message req; req.id = iap2::MsgId::RequestAuthenticationCert;
    s.onMessage(req);
    iap2::Message fail; fail.id = iap2::MsgId::AuthenticationFailed;
    fail.payload = {0, 5, 0, 0, 0x05};
    auto out = s.onMessage(fail);
    CHECK(out.empty());
    CHECK(s.state() == iap2::AuthState::Failed);
    CHECK(s.lastFailStatus() == 0x05);
}

static void test_out_of_order() {
    mfi::MockSigner signer;
    log::BufferLogger log;
    iap2::AuthSession s(signer, log);
    iap2::Message chal;
    chal.id = iap2::MsgId::RequestAuthChallengeResponse;
    chal.payload = {0, 6, 0, 0, 9, 9};
    auto out = s.onMessage(chal);
    CHECK(out.size() == 1);
    if (out.size() != 1) return;
    CHECK(out[0].id == iap2::MsgId::AuthenticationResponse);
    bool warned = false;
    for (auto& l : log.lines()) if (l.first == "warn") warned = true;
    CHECK(warned);
}


#include "iap2/link.hpp"

static void test_link_roundtrip() {
    iap2::Message m;
    m.id = iap2::MsgId::AuthenticationCertificate;
    m.payload = {0x30, 0x0a, 0x02, 0x01, 0x00, 0x05, 0x00};
    auto frame = iap2::LinkEncoder::encode(m);
    CHECK(frame.size() == 3 + 4 + m.payload.size());
    CHECK(frame[0] == 0xFF);
    CHECK(frame[1] == 4 + m.payload.size());

    iap2::LinkDecoder d;
    auto got = d.feed(frame.data(), frame.size());
    CHECK(got.size() == 1);
    CHECK(got[0].id == m.id);
    CHECK(got[0].payload == m.payload);
}

static void test_link_resync() {
    iap2::Message m;
    m.id = iap2::MsgId::RequestAuthenticationCert;
    auto frame = iap2::LinkEncoder::encode(m);

    // Garbage before a valid frame: decoder should skip and recover.
    std::vector<std::uint8_t> junk = {0x00, 0x11, 0x22, 0x33};
    std::vector<std::uint8_t> combined = junk;
    combined.insert(combined.end(), frame.begin(), frame.end());

    iap2::LinkDecoder d;
    auto got = d.feed(combined.data(), combined.size());
    CHECK(got.size() == 1);
    CHECK(got[0].id == m.id);
}

static void test_link_partial_feed() {
    iap2::Message m;
    m.id = iap2::MsgId::RequestAuthChallengeResponse;
    m.payload = {1, 2, 3, 4, 5, 6, 7, 8};
    auto frame = iap2::LinkEncoder::encode(m);

    iap2::LinkDecoder d;
    // Feed in two chunks.
    auto a = d.feed(frame.data(), 3);
    CHECK(a.empty());
    auto b = d.feed(frame.data() + 3, frame.size() - 3);
    CHECK(b.size() == 1);
    CHECK(b[0].id == m.id);
}

static void test_rejected_challenge_cannot_succeed() {
    mfi::MockSigner signer;
    log::BufferLogger log;
    iap2::AuthSession s(signer, log);
    iap2::Message challenge;
    challenge.id = iap2::MsgId::RequestAuthChallengeResponse;
    challenge.payload = {1, 2, 3};
    CHECK(s.onMessage(challenge).empty());
    CHECK(s.state() == iap2::AuthState::Failed);
    iap2::Message ok;
    ok.id = iap2::MsgId::AuthenticationSucceeded;
    CHECK(s.onMessage(ok).empty());
    CHECK(s.state() == iap2::AuthState::Failed);
    s.reset();
    CHECK(s.onMessage(ok).empty());
    CHECK(s.state() == iap2::AuthState::Idle);
}


#include "iap2/link_handshake.hpp"
#include "iap2/iap2_link.hpp"
#include <algorithm>

class PhoneCaptureTransport : public transport::Transport {
public:
    std::vector<std::uint8_t> bytes = {
        0xff,0x55,0x02,0,0xee,0x10,
        0xff,0x5a,0,0x1d,0xc0,0x75,0x63,0,0xf2,1,0x7f,0xff,0xff,
        7,0xd0,0,0x16,4,3,0x0a,0,1,0x0b,2,1,0x0c,1,2,0x66};
    std::vector<std::vector<std::uint8_t>> writes;
    std::size_t offset = 0;
    std::size_t chunk = 2;
    bool open() override { return true; }
    int write(const std::uint8_t* p, std::size_t n) override { writes.emplace_back(p,p+n); return n; }
    int read(std::uint8_t* p, std::size_t n, int) override {
        if (offset == bytes.size()) return -1;
        n = std::min({n, chunk, bytes.size()-offset});
        std::copy_n(bytes.data()+offset, n, p); offset += n; return n;
    }
    void close() override {}
};
static void test_live_phone_handshake() {
    log::BufferLogger logger;
    PhoneCaptureTransport transport;
    iap2::LinkHandshake handshake(transport, logger);
    CHECK(handshake.perform());
    CHECK(transport.writes.size() == 3);
    if (transport.writes.size() != 3) return;
    const auto& ack = transport.writes.back();
    CHECK(ack.size() == 9);
    CHECK(ack[5] == 0x63);
    CHECK(ack[6] == 0x75);
    unsigned sum = 0; for (auto b : ack) sum += b;
    CHECK((sum & 255) == 0);
    PhoneCaptureTransport coalesced;
    coalesced.chunk = 4096;
    coalesced.bytes.push_back(0xab);
    coalesced.bytes.push_back(0xcd);
    iap2::LinkHandshake withTail(coalesced, logger);
    CHECK(withTail.perform());
    CHECK(withTail.pendingBytes() == std::vector<std::uint8_t>({0xab,0xcd}));
    iap2::Message msg; msg.id = iap2::MsgId::RequestAuthenticationCert;
    const auto frame = iap2::Iap2LinkEncoder::encode(msg,0x64,0x75);
    CHECK(frame[7] == 10);
    CHECK(frame[9] == 0x40 && frame[10] == 0x40);
    iap2::Iap2LinkDecoder decoder;
    auto decoded = decoder.feed(frame.data(),frame.size());
    CHECK(decoded.size() == 1);
    if (!decoded.empty()) CHECK(decoded[0].id == msg.id);
}


#include "airplay/auth_setup.hpp"
#include "airplay_crypto.h"
#include <openssl/evp.h>
class DigestRecordingSigner : public mfi::MockSigner {
public:
    mutable std::vector<std::uint8_t> digest;
    std::vector<std::uint8_t> sign(mfi::ByteView input, mfi::SignatureEncoding enc) const override {
        digest.assign(input.data,input.data+input.size);
        return mfi::MockSigner::sign(input,enc);
    }
};
static void test_mfi_sap() {
    using Bytes = std::vector<std::uint8_t>;
    DigestRecordingSigner signer;
    CHECK(!airplay::mfiAuthSetup({},signer));
    Bytes zero(33);zero[0]=1;
    CHECK(!airplay::mfiAuthSetup(zero,signer));
    const auto peer = fxchain::airplay::x25519Generate();
    Bytes request{1};request.insert(request.end(),peer.pub.begin(),peer.pub.end());
    const auto response=airplay::mfiAuthSetup(request,signer);
    CHECK(response.has_value()); if(!response) return;
    auto read32=[&](std::size_t o){std::size_t n=0;for(int i=0;i<4;++i)n=(n<<8)|(*response)[o+i];return n;};
    const auto cert=signer.certificateChain()[0];
    CHECK(read32(32)==cert.size());
    CHECK(std::equal(cert.begin(),cert.end(),response->begin()+36));
    CHECK(read32(36+cert.size())==64);
    Bytes pub(response->begin(),response->begin()+32), both=pub;
    both.insert(both.end(),peer.pub.begin(),peer.pub.end());
    Bytes expected(32);unsigned hashSize=0;
    EVP_Digest(both.data(),both.size(),expected.data(),&hashSize,EVP_sha256(),nullptr);
    CHECK(signer.digest==expected);
    const auto shared=fxchain::airplay::x25519SharedSecret(peer.priv,pub);
    auto derive=[&](std::string label){Bytes b(label.begin(),label.end());b.insert(b.end(),shared.begin(),shared.end());Bytes d(20);unsigned n=0;EVP_Digest(b.data(),b.size(),d.data(),&n,EVP_sha1(),nullptr);d.resize(16);return d;};
    const auto key=derive("AES-KEY"),iv=derive("AES-IV");
    EVP_CIPHER_CTX* ctx=EVP_CIPHER_CTX_new();
    Bytes plain(80);int n=0,tail=0;
    CHECK(EVP_DecryptInit_ex(ctx,EVP_aes_128_ctr(),nullptr,key.data(),iv.data())==1);
    CHECK(EVP_DecryptUpdate(ctx,plain.data(),&n,response->data()+40+cert.size(),64)==1);
    CHECK(EVP_DecryptFinal_ex(ctx,plain.data()+n,&tail)==1);
    EVP_CIPHER_CTX_free(ctx);plain.resize(n+tail);
    CHECK(plain==signer.mfi::MockSigner::sign(mfi::ByteView(expected),mfi::SignatureEncoding::RawRS));
}

int main() {
    test_mfi_sap();
    test_live_phone_handshake();
    test_rejected_challenge_cannot_succeed();
    test_mock_signer();
    test_message_roundtrip();
    test_auth_happy_path();
    test_auth_failure();
    test_out_of_order();
    test_link_roundtrip();
    test_link_resync();
    test_link_partial_feed();
    std::printf("%d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
