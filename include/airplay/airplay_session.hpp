#pragma once
#include "log/logger.hpp"
#include "mfi/signer.hpp"
#include "airplay/pairing.hpp"
#include "airplay/audio_stream.hpp"
#include "airplay/mac_audio.hpp"
#include "airplay/screen_stream.hpp"
#include "airplay/aux_channels.hpp"
#include "airplay_crypto.h"
#include "transport/transport.hpp"
#include <cstdint>
#include <memory>
#include <map>
#include <vector>

namespace airplay {

// AirPlay RTSP control and stream setup for the wireless receiver prototype.
//
// This implements the request framing and the pairing handshake that the
// iPhone performs after it discovers the receiver over Bonjour. Screen
// packets arrive on the separate TCP receiver in ScreenStreamReceiver.
//
// Wire framing: RTSP requests/responses start as plain text over TCP. After
// successful pair-verify, records use the negotiated ChaCha20-Poly1305 control
// keys and counters; their plaintext remains RTSP with optional binary bodies.
// The pairing endpoints are:
//   GET /info      -> plist response with receiver capabilities
//   POST /pair-setup -> HomeKit TLV8/SRP-6a pairing exchange
//   POST /pair-verify -> X25519/Ed25519 controller verification
//
// AirPlay identity is separate from the iAP2 MFi signer. A shared pairing
// object can be passed by the server to retain identity and controller keys
// between accepted RTSP connections in the same process.
class AirPlaySession {
public:
    AirPlaySession(transport::Transport& t, mfi::Signer& signer, log::ILogger& log,
                   PairingProtocol* pairing = nullptr);
    ~AirPlaySession();

    // Run one RTSP connection until the peer closes or an error occurs.
    // Returns true if pairing reached at least the /pair-setup response.
    bool run();

private:
    bool handleRequest(const std::string& method,
                       const std::string& path,
                       const std::string& headers,
                       const std::vector<std::uint8_t>& body,
                       bool& pairing_seen);
    void writeResponse(int status,
                       const std::string& reason,
                       const std::string& content_type,
                       const std::vector<std::uint8_t>& body);
    bool decryptControlRecords();
    std::vector<std::uint8_t> encryptControlRecords(
        const std::vector<std::uint8_t>& plaintext);

    transport::Transport& t_;
    mfi::Signer& signer_;
    log::ILogger& log_;
    std::unique_ptr<PairingProtocol> owned_pairing_;
    PairingProtocol* pairing_ = nullptr;
    std::shared_ptr<AirPlayAuxChannels> aux_channels_;
    std::unique_ptr<ScreenStreamReceiver> screen_stream_;
    std::map<std::uint64_t, std::unique_ptr<AudioStreamReceiver>> audio_streams_;
    std::uint64_t screen_frame_count_ = 0;
    std::string recv_buf_;
    std::string response_cseq_;
    fxchain::airplay::Bytes encrypted_recv_buf_;
    fxchain::airplay::Bytes control_read_key_;
    fxchain::airplay::Bytes control_write_key_;
    std::uint64_t control_read_counter_ = 0;
    std::uint64_t control_write_counter_ = 0;
    bool control_cipher_enabled_ = false;
    bool pair_verify_request_seen_ = false;
};

}  // namespace airplay
