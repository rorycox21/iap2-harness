// Receives the encrypted CarPlay screen stream described by the local DiPlay
// source. Header layout, opcodes, key derivation, and framing are UNVERIFIED
// against a live iPhone capture.
#pragma once

#include "airplay_crypto.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace airplay {

class ScreenStreamReceiver {
public:
    using ConfigHandler = std::function<void(const std::string& codec,
                                             const fxchain::airplay::Bytes& config)>;
    using FrameHandler = std::function<void(const fxchain::airplay::Bytes& annex_b,
                                            std::uint64_t sender_nanos,
                                            std::uint64_t arrival_nanos)>;

    ScreenStreamReceiver(fxchain::airplay::Bytes key,
                         std::string expected_peer,
                         ConfigHandler on_config,
                         FrameHandler on_frame);
    ~ScreenStreamReceiver();

    ScreenStreamReceiver(const ScreenStreamReceiver&) = delete;
    ScreenStreamReceiver& operator=(const ScreenStreamReceiver&) = delete;

    // Binds on the local interfaces and starts a TCP accept/read worker.
    // Returns the ephemeral port, or 0 on failure.
    std::uint16_t start();
    void close();

private:
    void acceptLoop();
    void receiveLoop(int client);
    bool onMessage(const fxchain::airplay::Bytes& header,
                   const fxchain::airplay::Bytes& body);

    fxchain::airplay::Bytes key_;
    std::string expected_peer_;
    ConfigHandler on_config_;
    FrameHandler on_frame_;
    std::atomic<int> listen_fd_{-1};
    std::atomic<int> client_fd_{-1};
    std::uint16_t port_ = 0;
    std::thread worker_;
    std::atomic<bool> closing_{false};
    std::uint64_t frame_counter_ = 0;
};

} // namespace airplay
