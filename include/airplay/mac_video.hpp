// Mac window and VideoToolbox output for the received CarPlay screen stream.
// Codec parameter parsing and compatibility with a live phone remain UNVERIFIED.
#pragma once

#include "airplay_crypto.h"
#include <functional>
#include <string>

namespace airplay {

// Starts the platform display event loop and invokes network_task on a worker.
// On non-macOS platforms, the task runs directly and no window is available.
int runVideoDisplayApplication(std::function<void()> network_task);

// Thread-safe ingress from the AirPlay receiver.
void configureVideoDisplay(const std::string& codec,
                           const fxchain::airplay::Bytes& config);
void submitVideoFrame(const fxchain::airplay::Bytes& annex_b);

// Installs the callback for mouse-as-touch input in CarPlay display pixels.
void setVideoTouchHandler(std::function<void(bool, std::uint16_t, std::uint16_t)> handler);

} // namespace airplay
