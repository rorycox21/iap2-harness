#pragma once
// Full-duplex process transport for PlayPort's macOS bt-bridge. The bridge
// executable and iPhone address are supplied at runtime; Bluetooth behavior
// remains owned by the external helper.
#include "transport/transport.hpp"
#include <string>

namespace transport {
class BtBridgeTransport final : public Transport {
public:
    BtBridgeTransport(std::string executable, std::string address);
    ~BtBridgeTransport() override;
    bool open() override;
    int write(const std::uint8_t* data, std::size_t n) override;
    int read(std::uint8_t* data, std::size_t n, int timeout_ms) override;
    void close() override;
private:
    std::string executable_;
    std::string address_;
    int to_child_ = -1;
    int from_child_ = -1;
    int child_pid_ = -1;
};
}  // namespace transport
