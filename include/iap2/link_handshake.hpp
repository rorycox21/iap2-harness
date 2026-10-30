#pragma once
#include "log/logger.hpp"
#include "transport/transport.hpp"
#include <cstdint>
#include <vector>

namespace iap2 {

// Session descriptor used inside the SynchronizationPayload.
struct SessionDescriptor {
    std::uint8_t id;
    std::uint8_t type;
    std::uint8_t version;
};

// Link Synchronization Payload parameters, matching the field order and
// values from the decompiled DiPlay/xcertplay Iap2LinkEngine.localSynchronization().
struct SynParams {
    std::uint8_t  version                    = 0x01;
    std::uint8_t  maxOutgoing                = 0x05;
    std::uint16_t maxLength                  = 0xFFFF;
    std::uint16_t retransmissionTimeoutMs    = 2000;
    std::uint16_t acknowledgementTimeoutMs   = 22;
    std::uint8_t  maxRetransmissions         = 4;
    std::uint8_t  maxAcknowledgements        = 3;
    // Control, EA, file transfer — matching the reference implementation.
    std::vector<SessionDescriptor> sessions  = {
        {10, 0, 1},   // control
        {11, 2, 1},   // EA
        {12, 1, 2},   // file transfer
    };
};

class LinkHandshake {
public:
    LinkHandshake(transport::Transport& t, log::ILogger& log);
    bool perform();
    // Send only the local marker and SYN; useful when the caller captures raw peer bytes.
    bool sendSynOnly();
    const std::vector<std::uint8_t>& pendingBytes() const { return pending_; }
private:
    std::vector<std::uint8_t> pending_;
    transport::Transport& t_;
    log::ILogger& log_;
    bool sendMarker();
    bool sendSyn(const SynParams& p);
    bool waitForPeerSyn();
    std::vector<std::uint8_t> buildSynPayload(const SynParams& p) const;
};

}  // namespace iap2
