#include "platform/media_backend.hpp"
#include "airplay/mac_video.hpp"
#include "airplay/mac_audio.hpp"
#include <utility>
namespace platform {
namespace {
class MacMediaBackend final : public MediaBackend {
    DisplayConfig display_;
public:
    explicit MacMediaBackend(DisplayConfig display) : display_(display) {
        airplay::setVideoDisplaySize(display.width, display.height);
    }
    DisplayConfig displayConfig() const override { return display_; }
    const char* name() const override { return "macos"; }
    MediaCapabilities capabilities() const override { return {true, true, true}; }
    int run(std::function<void()> task) override { return airplay::runVideoDisplayApplication(std::move(task)); }
    void resetVideo() override { airplay::resetVideoDisplay(); }
    void configureVideo(const std::string& codec, const Bytes& config) override { airplay::configureVideoDisplay(codec, config); }
    void submitVideo(const Bytes& frame) override { airplay::submitVideoFrame(frame); }
    void setTouchHandler(TouchHandler handler) override { airplay::setVideoTouchHandler(std::move(handler)); }
    bool configureAudio(std::uint32_t rate, std::uint8_t channels) override { return airplay::configurePcmAudioOutput(rate, channels); }
    void submitPcm16Le(const Bytes& packet) override { airplay::submitPcmAudio(packet); }
    void stopAudio() override { airplay::stopPcmAudioOutput(); }
};
}
std::shared_ptr<MediaBackend> createMediaBackend(DisplayConfig display) { return std::make_shared<MacMediaBackend>(display); }
}
