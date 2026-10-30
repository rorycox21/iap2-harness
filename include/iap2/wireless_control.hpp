#pragma once
// Builds the source-backed iAP2 Wi-Fi configuration response. The configured
// network values must describe the active Mac hotspot; no guessed TLV fields
// are emitted beyond the DiPlay schema.
#include "iap2/message.hpp"
#include "log/logger.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace iap2 {

struct WirelessControlConfig {
    std::string ssid = "CarPlay";
    std::string passphrase;
    std::uint8_t security_type = 2;  // WPA/WPA2, per the reference enum.
    std::uint8_t channel = 36;
    std::string wireless_ip;
    std::uint16_t airplay_port = 7000;
    std::string device_identifier;
    std::string public_key;
    std::string source_version = "950.7.1";
};

class WirelessControl {
public:
    WirelessControl(log::ILogger& log, WirelessControlConfig config);
    std::vector<Message> onMessage(const Message& in);
private:
    log::ILogger& log_;
    WirelessControlConfig config_;
    bool transport_notified_ = false;
    unsigned pre_transport_config_sends_ = 0;
    unsigned post_transport_config_sends_ = 0;
};

}  // namespace iap2
