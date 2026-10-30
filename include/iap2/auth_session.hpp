#pragma once
#include "iap2/message.hpp"
#include "log/logger.hpp"
#include "mfi/signer.hpp"
#include <cstdint>
#include <vector>

namespace iap2 {

enum class AuthState {
    Idle,
    CertRequested,
    ChallengeRequested,
    Succeeded,
    Failed,
};

class AuthSession {
public:
    AuthSession(mfi::Signer& signer, log::ILogger& log);
    std::vector<Message> onMessage(const Message& in);
    AuthState state() const { return state_; }
    std::uint8_t lastFailStatus() const { return last_fail_status_; }
    void reset();
private:
    mfi::Signer& signer_;
    log::ILogger& log_;
    AuthState state_ = AuthState::Idle;
    std::uint8_t last_fail_status_ = 0;
};

}  // namespace iap2
