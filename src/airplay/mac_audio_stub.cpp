// Non-macOS fallback for the macOS AirPlay PCM output API.
// Audio playback support is unavailable and remains UNVERIFIED off macOS.
#include "airplay/mac_audio.hpp"

namespace airplay {
bool configurePcmAudioOutput(std::uint32_t, std::uint8_t) { return false; }
void submitPcmAudioRtp(const fxchain::airplay::Bytes&) {}
void stopPcmAudioOutput() {}
} // namespace airplay
