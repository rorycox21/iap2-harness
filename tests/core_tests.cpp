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
    CHECK(wire[0] == 0xAA && wire[1] == 0x03);
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
    CHECK(out[0].id == iap2::MsgId::AuthenticationCertificate);
    CHECK(!out[0].payload.empty());
    CHECK(s.state() == iap2::AuthState::CertRequested);
    iap2::Message chal;
    chal.id = iap2::MsgId::RequestAuthChallengeResponse;
    chal.payload = {1, 2, 3, 4, 5};
    out = s.onMessage(chal);
    CHECK(out.size() == 1);
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
    fail.payload = {0x05};
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
    chal.payload = {9, 9};
    auto out = s.onMessage(chal);
    CHECK(out.size() == 1);
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

int main() {
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
