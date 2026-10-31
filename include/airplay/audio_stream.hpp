// Receives and decrypts AirPlay type-100 RTP audio packets using the local
// DiPlay framing. Packet framing is source-derived and remains UNVERIFIED
// against a live iPhone; this class does not decode or play audio.
#pragma once

#include "airplay_crypto.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <utility>

namespace airplay {

// Convert the existing fixed-12-byte AirPlay PCM RTP packet format to
// interleaved S16LE for all output backends. Empty means malformed/unsupported.
fxchain::airplay::Bytes pcm16LeFromAirPlayRtp(const fxchain::airplay::Bytes& packet,
                                            std::uint8_t channels);

class AudioStreamReceiver {
public:
    using PacketHandler = std::function<void(const fxchain::airplay::Bytes& rtp)>;

    AudioStreamReceiver(fxchain::airplay::Bytes key, std::string expected_peer,
                        PacketHandler on_packet);
    ~AudioStreamReceiver();
    AudioStreamReceiver(const AudioStreamReceiver&) = delete;
    AudioStreamReceiver& operator=(const AudioStreamReceiver&) = delete;

    // Binds and starts data/control UDP sockets. Returns data and control ports.
    std::pair<std::uint16_t, std::uint16_t> start();
    void close();

private:
    void dataLoop();
    void controlLoop();
    bool peerMatches(const struct sockaddr_storage& peer) const;

    fxchain::airplay::Bytes key_;
    std::string expected_peer_;
    PacketHandler on_packet_;
    std::atomic<int> data_fd_{-1};
    std::atomic<int> control_fd_{-1};
    std::thread data_thread_;
    std::thread control_thread_;
    std::atomic<bool> closing_{false};
};

} // namespace airplay
