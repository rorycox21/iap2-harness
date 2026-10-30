// macOS PCM audio output for decrypted AirPlay type-100 RTP packets.
// Packet byte order follows the local DiPlay renderer; live compatibility is
// UNVERIFIED until exercised with an iPhone.
#pragma once

#include "airplay_crypto.h"
#include <cstdint>

namespace airplay {

bool configurePcmAudioOutput(std::uint32_t sample_rate, std::uint8_t channels);
void submitPcmAudioRtp(const fxchain::airplay::Bytes& decrypted_rtp);
void stopPcmAudioOutput();

} // namespace airplay
