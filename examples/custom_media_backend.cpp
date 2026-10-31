// Compileable API example, not a Pi/QNX renderer. Replace these no-op output
// methods with the target SDK implementation. Keep advertised capabilities
// false until the corresponding output/input device is implemented.
#include "platform/media_backend.hpp"
#include <cstdio>
namespace platform {
namespace {
class ExampleBackend final : public MediaBackend {
    DisplayConfig display_;
public:
    explicit ExampleBackend(DisplayConfig display) : display_(display) {}
    DisplayConfig displayConfig() const override { return display_; }
    const char* name() const override { return "example-no-output"; }
    MediaCapabilities capabilities() const override { return {false, false, false}; }
    int run(std::function<void()> task) override {
        std::fprintf(stderr, "example backend: implement native output before using on hardware\n");
        task(); return 0;
    }
    void resetVideo() override {}
    void configureVideo(const std::string&, const Bytes&) override {}
    void submitVideo(const Bytes&) override {}
    void setTouchHandler(TouchHandler) override {}
    bool configureAudio(std::uint32_t, std::uint8_t) override { return false; }
    void submitPcm16Le(const Bytes&) override {}
    void stopAudio() override {}
};
}
std::shared_ptr<MediaBackend> createMediaBackend(DisplayConfig display) { return std::make_shared<ExampleBackend>(display); }
}
