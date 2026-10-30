// Native macOS VideoToolbox decoder and AppKit window for CarPlay video.
// AirPlay codec config/frame parsing is based on local source and UNVERIFIED
// against a live iPhone stream.
#include "airplay/mac_video.hpp"

#import <AppKit/AppKit.h>
#import <CoreImage/CoreImage.h>
#import <CoreVideo/CoreVideo.h>
#import <VideoToolbox/VideoToolbox.h>
#import <dispatch/dispatch.h>

#include <atomic>
#include <algorithm>
#include <cmath>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace {
std::mutex g_touch_mutex;
std::function<void(bool, std::uint16_t, std::uint16_t)> g_touch_handler;
bool g_pointer_down = false;

void dispatchCarPlayPointer(NSView* view, NSEvent* event, int phase) {
    const NSPoint point = [view convertPoint:event.locationInWindow fromView:nil];
    const NSRect bounds = view.bounds;
    if (bounds.size.width <= 0 || bounds.size.height <= 0) return;
    constexpr CGFloat display_aspect = 1024.0 / 600.0;
    NSRect display = bounds;
    if (bounds.size.width / bounds.size.height > display_aspect) {
        display.size.width = bounds.size.height * display_aspect;
        display.origin.x = (bounds.size.width - display.size.width) / 2.0;
    } else {
        display.size.height = bounds.size.width / display_aspect;
        display.origin.y = (bounds.size.height - display.size.height) / 2.0;
    }

    std::function<void(bool, std::uint16_t, std::uint16_t)> handler;
    bool down = false;
    {
        std::lock_guard<std::mutex> lock(g_touch_mutex);
        if (phase == 0) {
            if (!NSPointInRect(point, display)) return;
            g_pointer_down = true;
        } else if (!g_pointer_down) {
            return;
        }
        down = phase != 2;
        if (phase == 2) g_pointer_down = false;
        handler = g_touch_handler;
    }
    if (!handler) return;

    const double x_ratio = std::clamp((point.x - display.origin.x) / display.size.width, 0.0, 1.0);
    const double y_ratio = std::clamp((NSMaxY(display) - point.y) / display.size.height, 0.0, 1.0);
    const auto x = static_cast<std::uint16_t>(std::lround(x_ratio * 1024.0));
    const auto y = static_cast<std::uint16_t>(std::lround(y_ratio * 600.0));
    handler(down, x, y);
}
}  // namespace

@interface CarPlayInputView : NSImageView
@end

@implementation CarPlayInputView
- (BOOL)acceptsFirstResponder { return YES; }
- (void)mouseDown:(NSEvent*)event {
    [self.window makeFirstResponder:self];
    dispatchCarPlayPointer(self, event, 0);
}
- (void)mouseDragged:(NSEvent*)event { dispatchCarPlayPointer(self, event, 1); }
- (void)mouseUp:(NSEvent*)event { dispatchCarPlayPointer(self, event, 2); }
@end

@interface CarPlayWindowDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@end
@implementation CarPlayWindowDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}
@end

namespace airplay {
using fxchain::airplay::Bytes;
namespace {
struct DisplayState {
    NSWindow* window = nil;
    NSImageView* image_view = nil;
    CarPlayWindowDelegate* delegate = nil;
    CIContext* ci_context = nil;
    VTDecompressionSessionRef session = nullptr;
    CMVideoFormatDescriptionRef format = nullptr;
    std::mutex mutex;
    std::string codec;
    Bytes config;
};
DisplayState g;

std::uint16_t be16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

bool h264Parameters(const Bytes& config, std::vector<Bytes>& params) {
    if (config.size() < 7 || config[0] != 1) return false;
    std::size_t pos = 5;
    const unsigned sps_count = config[pos++] & 0x1f;
    for (unsigned i = 0; i < sps_count; ++i) {
        if (pos + 2 > config.size()) return false;
        const auto n = be16(config.data() + pos); pos += 2;
        if (!n || pos + n > config.size()) return false;
        params.emplace_back(config.begin() + pos, config.begin() + pos + n); pos += n;
    }
    if (pos >= config.size()) return false;
    const unsigned pps_count = config[pos++];
    for (unsigned i = 0; i < pps_count; ++i) {
        if (pos + 2 > config.size()) return false;
        const auto n = be16(config.data() + pos); pos += 2;
        if (!n || pos + n > config.size()) return false;
        params.emplace_back(config.begin() + pos, config.begin() + pos + n); pos += n;
    }
    return params.size() >= 2;
}

bool h265Parameters(const Bytes& config, std::vector<Bytes>& params) {
    if (config.size() < 23 || config[0] != 1) return false;
    std::size_t pos = 23;
    const unsigned arrays = config[22];
    for (unsigned i = 0; i < arrays; ++i) {
        if (pos + 3 > config.size()) return false;
        const unsigned type = config[pos++] & 0x3f;
        const unsigned count = be16(config.data() + pos); pos += 2;
        for (unsigned j = 0; j < count; ++j) {
            if (pos + 2 > config.size()) return false;
            const auto n = be16(config.data() + pos); pos += 2;
            if (!n || pos + n > config.size()) return false;
            if (type == 32 || type == 33 || type == 34)
                params.emplace_back(config.begin() + pos, config.begin() + pos + n);
            pos += n;
        }
    }
    return params.size() >= 3;
}

void releaseFormat() {
    if (g.session) { VTDecompressionSessionInvalidate(g.session); CFRelease(g.session); g.session = nullptr; }
    if (g.format) { CFRelease(g.format); g.format = nullptr; }
}

void decoded(void*, void* source, OSStatus status, VTDecodeInfoFlags,
             CVImageBufferRef image, CMTime, CMTime) {
    if (status != noErr || !image) return;
    CVPixelBufferRef pixel = (CVPixelBufferRef)image;
    CFRetain(pixel);
    dispatch_async(dispatch_get_main_queue(), ^{
        CIImage* ci = [CIImage imageWithCVPixelBuffer:pixel];
        CGImageRef cg = [g.ci_context createCGImage:ci fromRect:ci.extent];
        if (cg) {
            NSImage* ns = [[NSImage alloc] initWithCGImage:cg size:NSZeroSize];
            g.image_view.image = ns;
            [g.window setTitle:[NSString stringWithFormat:@"CarPlay — %zu × %zu",
                               CGImageGetWidth(cg), CGImageGetHeight(cg)]];
            CGImageRelease(cg);
        }
        CVPixelBufferRelease(pixel);
    });
    (void)source;
}

void makeDecoderLocked() {
    releaseFormat();
    std::vector<Bytes> params;
    const bool h264 = g.codec == "H264" && h264Parameters(g.config, params);
    const bool h265 = g.codec == "H265" && h265Parameters(g.config, params);
    if (!h264 && !h265) return;
    std::vector<const uint8_t*> pointers;
    std::vector<size_t> sizes;
    for (const auto& p : params) { pointers.push_back(p.data()); sizes.push_back(p.size()); }
    OSStatus result = h264
        ? CMVideoFormatDescriptionCreateFromH264ParameterSets(kCFAllocatorDefault,
              params.size(), pointers.data(), sizes.data(), 4, &g.format)
        : CMVideoFormatDescriptionCreateFromHEVCParameterSets(kCFAllocatorDefault,
              params.size(), pointers.data(), sizes.data(), 4, nullptr, &g.format);
    if (result != noErr || !g.format) { g.format = nullptr; return; }
    VTDecompressionOutputCallbackRecord cb{decoded, nullptr};
    result = VTDecompressionSessionCreate(kCFAllocatorDefault, g.format, nullptr,
                                           nullptr, &cb, &g.session);
    if (result != noErr) releaseFormat();
}

bool annexBToSample(const Bytes& input, Bytes& output) {
    auto startCode = [&input](std::size_t from, std::size_t& prefix) {
        for (std::size_t i = from; i + 3 <= input.size(); ++i) {
            if (input[i] == 0 && input[i+1] == 0 && input[i+2] == 1) { prefix=3; return i; }
            if (i + 4 <= input.size() && input[i] == 0 && input[i+1] == 0 &&
                input[i+2] == 0 && input[i+3] == 1) { prefix=4; return i; }
        }
        return input.size();
    };
    std::size_t prefix = 0, pos = startCode(0, prefix);
    if (pos == input.size()) return false;
    while (pos < input.size()) {
        const std::size_t nal = pos + prefix;
        std::size_t next_prefix = 0;
        const std::size_t next = startCode(nal, next_prefix);
        const std::size_t end = next == input.size() ? input.size() : next;
        if (end <= nal || end - nal > UINT32_MAX) return false;
        const auto n = static_cast<std::uint32_t>(end - nal);
        output.insert(output.end(), {static_cast<std::uint8_t>(n >> 24),
                                     static_cast<std::uint8_t>(n >> 16),
                                     static_cast<std::uint8_t>(n >> 8),
                                     static_cast<std::uint8_t>(n)});
        output.insert(output.end(), input.begin() + nal, input.begin() + end);
        if (next == input.size()) break;
        pos = next; prefix = next_prefix;
    }
    return !output.empty();
}
} // namespace

int runVideoDisplayApplication(std::function<void()> network_task) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        g.delegate = [[CarPlayWindowDelegate alloc] init];
        [NSApp setDelegate:g.delegate];
        NSRect rect = NSMakeRect(100, 100, 1024, 600);
        g.window = [[NSWindow alloc] initWithContentRect:rect
            styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                      NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable)
            backing:NSBackingStoreBuffered defer:NO];
        [g.window setDelegate:g.delegate];
        [g.window setTitle:@"CarPlay receiver — waiting for video"];
        g.image_view = [[CarPlayInputView alloc] initWithFrame:[[g.window contentView] bounds]];
        g.image_view.imageScaling = NSImageScaleProportionallyUpOrDown;
        g.image_view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        [[g.window contentView] addSubview:g.image_view];
        g.ci_context = [CIContext contextWithOptions:nil];
        [g.window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        std::thread(std::move(network_task)).detach();
        [NSApp run];
        {
            std::lock_guard<std::mutex> lock(g.mutex);
            releaseFormat();
        }
        [NSApp setDelegate:nil];
        [g.delegate release];
        g.delegate = nil;
    }
    return 0;
}

void configureVideoDisplay(const std::string& codec, const Bytes& config) {
    std::lock_guard<std::mutex> lock(g.mutex);
    g.codec = codec;
    g.config = config;
    makeDecoderLocked();
}

void submitVideoFrame(const Bytes& annex_b) {
    std::lock_guard<std::mutex> lock(g.mutex);
    if (!g.session || !g.format) return;
    Bytes sample_bytes;
    if (!annexBToSample(annex_b, sample_bytes)) return;
    CMBlockBufferRef block = nullptr;
    if (CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, nullptr,
            sample_bytes.size(), kCFAllocatorDefault, nullptr, 0, sample_bytes.size(),
            0, &block) != noErr) return;
    if (CMBlockBufferReplaceDataBytes(sample_bytes.data(), block, 0,
                                      sample_bytes.size()) != noErr) {
        CFRelease(block); return;
    }
    const std::size_t sample_size = sample_bytes.size();
    CMSampleBufferRef sample = nullptr;
    const OSStatus made = CMSampleBufferCreateReady(kCFAllocatorDefault, block, g.format,
        1, 0, nullptr, 1, &sample_size, &sample);
    CFRelease(block);
    if (made != noErr || !sample) return;
    VTDecodeFrameFlags flags = kVTDecodeFrame_EnableAsynchronousDecompression;
    VTDecompressionSessionDecodeFrame(g.session, sample, flags, nullptr, nullptr);
    CFRelease(sample);
}

void setVideoTouchHandler(
    std::function<void(bool, std::uint16_t, std::uint16_t)> handler) {
    std::lock_guard<std::mutex> lock(g_touch_mutex);
    g_touch_handler = std::move(handler);
    if (!g_touch_handler) g_pointer_down = false;
}
} // namespace airplay
