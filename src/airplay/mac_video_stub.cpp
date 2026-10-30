// Non-macOS fallback for the Mac VideoToolbox display API. A real CarPlay UI
// requires macOS; codec and display behavior are UNVERIFIED on other systems.
#include "airplay/mac_video.hpp"

namespace airplay {
int runVideoDisplayApplication(std::function<void()> network_task) {
    network_task();
    return 0;
}
void configureVideoDisplay(const std::string&, const fxchain::airplay::Bytes&) {}
void submitVideoFrame(const fxchain::airplay::Bytes&) {}
} // namespace airplay
