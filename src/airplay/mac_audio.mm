// macOS AudioQueue output for source-format decrypted AirPlay PCM packets.
// The source advertises and sends multiple PCM rates; live timing and
// end-to-end audio remain UNVERIFIED until exercised with an iPhone.
#include "airplay/mac_audio.hpp"

#import <AudioToolbox/AudioToolbox.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

namespace airplay {
namespace {
struct AudioState {
    AudioQueueRef queue = nullptr;
    std::vector<AudioQueueBufferRef> free_buffers;
    std::mutex mutex;
    std::uint32_t sample_rate = 0;
    std::uint8_t channels = 0;
    bool started = false;
};
AudioState g_audio;

void bufferReturned(void* context, AudioQueueRef queue, AudioQueueBufferRef buffer) {
    auto* state = static_cast<AudioState*>(context);
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->queue == queue) state->free_buffers.push_back(buffer);
}
} // namespace

bool configurePcmAudioOutput(std::uint32_t sample_rate, std::uint8_t channels) {
    if ((channels != 1 && channels != 2) || sample_rate < 8000 || sample_rate > 48000)
        return false;
    stopPcmAudioOutput();

    AudioStreamBasicDescription format{};
    format.mSampleRate = sample_rate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger |
                          kLinearPCMFormatFlagIsPacked;
    format.mBytesPerPacket = channels * 2;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = channels * 2;
    format.mChannelsPerFrame = channels;
    format.mBitsPerChannel = 16;

    AudioQueueRef queue = nullptr;
    if (AudioQueueNewOutput(&format, bufferReturned, &g_audio, nullptr, nullptr,
                            0, &queue) != noErr || !queue)
        return false;
    std::vector<AudioQueueBufferRef> buffers;
    for (int i = 0; i < 8; ++i) {
        AudioQueueBufferRef buffer = nullptr;
        if (AudioQueueAllocateBuffer(queue, 16384, &buffer) != noErr || !buffer) {
            AudioQueueDispose(queue, true);
            return false;
        }
        buffers.push_back(buffer);
    }
    AudioQueueSetParameter(queue, kAudioQueueParam_Volume, 1.0f);
    {
        std::lock_guard<std::mutex> lock(g_audio.mutex);
        g_audio.queue = queue;
        g_audio.free_buffers = std::move(buffers);
        g_audio.sample_rate = sample_rate;
        g_audio.channels = channels;
        g_audio.started = false;
    }
    return true;
}

void submitPcmAudioRtp(const fxchain::airplay::Bytes& rtp) {
    if (rtp.size() <= 12) return;
    const std::size_t payload_size = rtp.size() - 12;
    if ((payload_size & 1) != 0 || payload_size > 16384) return;

    std::lock_guard<std::mutex> lock(g_audio.mutex);
    if (!g_audio.queue || g_audio.free_buffers.empty()) return;
    AudioQueueBufferRef buffer = g_audio.free_buffers.back();
    g_audio.free_buffers.pop_back();
    auto* output = static_cast<std::uint8_t*>(buffer->mAudioData);
    // DiPlay byte-swaps each signed 16-bit PCM sample before playback.
    for (std::size_t i = 0; i < payload_size; i += 2) {
        output[i] = rtp[13 + i];
        output[i + 1] = rtp[12 + i];
    }
    buffer->mAudioDataByteSize = static_cast<UInt32>(payload_size);
    if (AudioQueueEnqueueBuffer(g_audio.queue, buffer, 0, nullptr) != noErr) {
        g_audio.free_buffers.push_back(buffer);
        return;
    }
    if (!g_audio.started) {
        if (AudioQueueStart(g_audio.queue, nullptr) == noErr) g_audio.started = true;
    }
}

void stopPcmAudioOutput() {
    AudioQueueRef queue = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_audio.mutex);
        queue = g_audio.queue;
        g_audio.queue = nullptr;
        g_audio.free_buffers.clear();
        g_audio.sample_rate = 0;
        g_audio.channels = 0;
        g_audio.started = false;
    }
    if (queue) {
        AudioQueueStop(queue, true);
        AudioQueueDispose(queue, true);
    }
}

} // namespace airplay
