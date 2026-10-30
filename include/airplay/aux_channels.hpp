// AirPlay timing and encrypted event channels negotiated by initial RTSP
// SETUP. Packet formats follow local DiPlay source and remain UNVERIFIED
// against a live iPhone capture.
#pragma once

#include "airplay_crypto.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace airplay {
class AirPlayAuxChannels {
public:
    explicit AirPlayAuxChannels(fxchain::airplay::Bytes shared_secret);
    ~AirPlayAuxChannels();
    AirPlayAuxChannels(const AirPlayAuxChannels&) = delete;
    AirPlayAuxChannels& operator=(const AirPlayAuxChannels&) = delete;
    bool start();
    bool sendTouch(bool down, std::uint16_t x, std::uint16_t y);
    std::uint16_t timingPort() const { return timing_port_; }
    std::uint16_t eventPort() const { return event_port_; }
    void close();
private:
    void timingLoop();
    void eventAcceptLoop();
    void eventLoop(int client);
    bool sendEncryptedEvent(int client, const fxchain::airplay::Bytes& plaintext);
    fxchain::airplay::Bytes event_read_key_, event_write_key_;
    std::atomic<bool> closing_{false};
    std::atomic<int> timing_fd_{-1}, event_listen_fd_{-1}, event_client_fd_{-1};
    std::mutex event_write_mutex_;
    std::uint64_t event_write_counter_ = 0;
    std::uint32_t event_cseq_ = 0;
    std::uint16_t timing_port_ = 0, event_port_ = 0;
    std::thread timing_thread_, event_thread_;
};
} // namespace airplay
