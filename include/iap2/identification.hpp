#pragma once
#include "iap2/message.hpp"
#include "log/logger.hpp"
#include <vector>

namespace iap2 {

// iAP2 identification exchange, which runs before authentication.
//
// Sequence (from the USENIX CarPlay capture paper):
//   1. Accessory -> phone: StartIdentification (16 bytes)
//   2. Phone -> accessory: vehicle/device info (~320 bytes)
//   3. Accessory -> phone: IdentificationAccepted
//
// The exact payload layout is not fully documented publicly. The paper
// gives the message sizes and the order; the fields are UNVERIFIED.
struct IdentificationConfig {
    std::string name = "MacCarPlay";
    std::string model_identifier = "BMW-NBT";
    std::string manufacturer = "BMW";
    std::string serial_number = "NBT-MAC";
    std::string firmware_version = "1.0";
    std::string hardware_version = "Mac";
    std::string bluetooth_mac = "00:00:00:00:00:00";
    std::string ssid = "CarPlay";
    std::string external_accessory_protocol = "com.shilapi.xcertplay";
    std::string language = "en";
};

class Identification {
public:
    Identification(log::ILogger& log, IdentificationConfig config = {});
    std::vector<Message> startIdentification();
    std::vector<Message> onMessage(const Message& in);
    bool done() const { return done_; }
private:
    log::ILogger& log_;
    IdentificationConfig config_;
    bool done_ = false;
};

}  // namespace iap2
