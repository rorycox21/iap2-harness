#include "iap2/auth_session.hpp"
#include <cstdio>
#include <sstream>
#include <string>

namespace iap2 {

namespace {
std::vector<std::uint8_t> parameter0(const std::vector<std::uint8_t>& value) {
    const auto length = static_cast<std::uint16_t>(value.size() + 4);
    std::vector<std::uint8_t> out{
        static_cast<std::uint8_t>(length >> 8), static_cast<std::uint8_t>(length), 0x00, 0x00};
    out.insert(out.end(), value.begin(), value.end());
    return out;
}

bool readParameter0(const std::vector<std::uint8_t>& body,
                    std::vector<std::uint8_t>& value) {
    if (body.size() < 4) return false;
    const std::size_t length = (static_cast<std::size_t>(body[0]) << 8) | body[1];
    const std::uint16_t id = static_cast<std::uint16_t>((body[2] << 8) | body[3]);
    if (length < 4 || length > body.size() || id != 0) return false;
    value.assign(body.begin() + 4, body.begin() + length);
    return true;
}
}  // namespace

AuthSession::AuthSession(mfi::Signer& signer, log::ILogger& log)
    : signer_(signer), log_(log) {}

void AuthSession::reset() {
    state_ = AuthState::Idle;
    last_fail_status_ = 0;
}

static std::string hexPreview(const std::vector<std::uint8_t>& v, std::size_t max = 32) {
    std::ostringstream os;
    const std::size_t n = v.size() < max ? v.size() : max;
    for (std::size_t i = 0; i < n; ++i) {
        char buf[4];
        std::snprintf(buf, sizeof buf, "%02x ", v[i]);
        os << buf;
    }
    if (v.size() > max) os << "... (" << v.size() << " bytes)";
    return os.str();
}

std::vector<Message> AuthSession::onMessage(const Message& in) {
    std::vector<Message> out;
    switch (in.id) {
    case MsgId::AuthInformation:
        log_.info("auth: AuthInformation received (ignored)");
        return out;
    case MsgId::RequestAuthenticationCert: {
        log_.info("auth: RequestAuthenticationCertificate");
        if (state_ != AuthState::Idle)
            log_.warn("auth: certificate requested out of state (expected Idle)");
        Message resp;
        resp.id = MsgId::AuthenticationCertificate;
        for (const auto& der : signer_.certificateChain())
            resp.payload.insert(resp.payload.end(), der.begin(), der.end());
        resp.payload = parameter0(resp.payload);
        log_.debug("auth: sending AuthenticationCertificate, " +
                   std::to_string(resp.payload.size()) + " bytes");
        out.push_back(std::move(resp));
        state_ = AuthState::CertRequested;
        return out;
    }
    case MsgId::RequestAuthChallengeResponse: {
        log_.info("auth: RequestAuthenticationChallengeResponse");
        if (state_ != AuthState::CertRequested)
            log_.warn("auth: challenge received out of state (expected CertRequested)");
        std::vector<std::uint8_t> challenge;
        if (!readParameter0(in.payload, challenge)) {
            log_.error("auth: challenge missing parameter 0");
            state_ = AuthState::Failed;
            last_fail_status_ = 0x01;
            return out;
        }
        log_.debug("auth: challenge nonce " + hexPreview(challenge) +
                   " (" + std::to_string(challenge.size()) + " bytes)");
        const auto signature = signer_.sign(
            mfi::ByteView(challenge.data(), challenge.size()),
            mfi::SignatureEncoding::RawRS);
        if (signature.size() != 64) {
            log_.error("auth: signer failed to produce a 64-byte P-256 r || s signature");
            state_ = AuthState::Failed;
            last_fail_status_ = 0x02;
            return out;
        }
        Message resp;
        resp.id = MsgId::AuthenticationResponse;
        resp.payload = parameter0(signature);
        log_.debug("auth: sending AuthenticationResponse, " +
                   std::to_string(resp.payload.size()) + " bytes");
        out.push_back(std::move(resp));
        state_ = AuthState::ChallengeRequested;
        return out;
    }
    case MsgId::AuthenticationSucceeded:
        log_.info("auth: AuthenticationSucceeded");
        if (state_ != AuthState::ChallengeRequested) {
            log_.warn("auth: ignoring success before a challenge response");
            return out;
        }
        state_ = AuthState::Succeeded;
        return out;
    case MsgId::AuthenticationFailed: {
        std::vector<std::uint8_t> status;
        last_fail_status_ = readParameter0(in.payload, status) && !status.empty()
                                ? status[0] : 0;
        log_.error("auth: AuthenticationFailed, status=" +
                   std::to_string(last_fail_status_));
        state_ = AuthState::Failed;
        return out;
    }
    default:
        log_.warn("auth: unexpected message id");
        return out;
    }
}

}  // namespace iap2
