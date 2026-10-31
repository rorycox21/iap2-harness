#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace transport {

// Abstract byte pipe. No platform headers in this interface; libusb lives
// behind make_libusb(), and a QNX implementation will live behind a future
// make_qnx().
class Transport {
public:
    virtual ~Transport() = default;
    virtual bool open() = 0;
    // Returns bytes written, or negative on error.
    virtual int write(const std::uint8_t* data, std::size_t n) = 0;
    // Returns bytes read (0 on timeout), or negative on error.
    virtual int read(std::uint8_t* data, std::size_t n, int timeout_ms) = 0;
    virtual void close() = 0;
    // Connection metadata is available without downcasting to a socket backend.
    virtual std::string peerAddress() const { return {}; }
};

struct UsbConfig {
    std::uint16_t vid = 0;
    std::uint16_t pid = 0;
    int configuration = 0; // 0 preserves the active device configuration.
    int interface_number = 0;
    unsigned char ep_in  = 0x81;
    unsigned char ep_out = 0x02;
};

// Optional experimental USB host backend. A build with USB disabled returns
// nullptr with an explicit diagnostic; no device-mode/wired CarPlay is implied.
std::unique_ptr<Transport> make_libusb(const UsbConfig& cfg);

// Enumerates USB devices, prints them, returns count. Stage 3 implementation.
int list_usb_devices();

}  // namespace transport
