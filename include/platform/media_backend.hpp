#pragma once
#include <vector>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace platform {
using Bytes = std::vector<std::uint8_t>;
using TouchHandler = std::function<void(bool, std::uint16_t, std::uint16_t)>;
struct DisplayConfig {
    std::uint16_t width = 1024;
    std::uint16_t height = 600;
    std::uint16_t max_fps = 60;
    std::uint16_t physical_width_mm = 300;
    std::uint16_t physical_height_mm = 176;
};
struct MediaCapabilities { bool video; bool pcm_audio; bool touch; };

// One backend belongs to one receiver. Media ingress may arrive on worker
// threads. The backend owns its UI loop/decoder/output devices; protocol code
// owns stream decryption and framing. Touch coordinates use display pixels.
class MediaBackend {
public:
    virtual ~MediaBackend() = default;
    virtual const char* name() const = 0;
    virtual MediaCapabilities capabilities() const = 0;
    virtual DisplayConfig displayConfig() const = 0;
    virtual int run(std::function<void()> network_task) = 0;
    virtual void resetVideo() = 0;
    virtual void configureVideo(const std::string& codec, const Bytes& config) = 0;
    virtual void submitVideo(const Bytes& annex_b) = 0;
    virtual void setTouchHandler(TouchHandler handler) = 0;
    virtual bool configureAudio(std::uint32_t sample_rate, std::uint8_t channels) = 0;
    // Interleaved signed 16-bit LITTLE-ENDIAN PCM at the last configured
    // rate/channel count. RTP parsing and byte-order conversion stay in core.
    virtual void submitPcm16Le(const Bytes& samples) = 0;
    virtual void stopAudio() = 0;
};

// Implemented by the backend selected at configure time, never by the core.
std::shared_ptr<MediaBackend> createMediaBackend(DisplayConfig display = {});
} // namespace platform
