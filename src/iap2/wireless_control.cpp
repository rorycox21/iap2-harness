#include "iap2/wireless_control.hpp"

namespace iap2 {
namespace {
void put16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}
void param(std::vector<std::uint8_t>& out, std::uint16_t id,
           const std::vector<std::uint8_t>& value) {
    put16(out, static_cast<std::uint16_t>(value.size() + 4));
    put16(out, id);
    out.insert(out.end(), value.begin(), value.end());
}
void textParam(std::vector<std::uint8_t>& out, std::uint16_t id,
               const std::string& value) {
    std::vector<std::uint8_t> bytes(value.begin(), value.end());
    bytes.push_back(0);
    param(out, id, bytes);
}
bool parameter0(const std::vector<std::uint8_t>& body,
                std::vector<std::uint8_t>& value) {
    if (body.size() < 4) return false;
    const std::size_t length = (static_cast<std::size_t>(body[0]) << 8) | body[1];
    const std::uint16_t id = static_cast<std::uint16_t>((body[2] << 8) | body[3]);
    if (length < 4 || length > body.size() || id != 0) return false;
    value.assign(body.begin() + 4, body.begin() + length);
    return true;
}
}  // namespace

WirelessControl::WirelessControl(log::ILogger& log, WirelessControlConfig config)
    : log_(log), config_(std::move(config)) {}

std::vector<Message> WirelessControl::onMessage(const Message& in) {
    if (in.id == static_cast<MsgId>(0x4300)) {
        if (config_.ssid.empty() || config_.wireless_ip.empty() ||
            config_.airplay_port == 0 || config_.device_identifier.empty() ||
            config_.public_key.empty()) {
            log_.warn("wireless: cannot start CarPlay without SSID, Wi-Fi IP, port, and AirPlay identity");
            return {};
        }
        Message response;
        response.id = static_cast<MsgId>(0x4301);
        std::vector<std::uint8_t> wireless;
        textParam(wireless, 0, config_.ssid);
        textParam(wireless, 1, config_.passphrase);
        param(wireless, 2, {config_.channel});
        textParam(wireless, 3, config_.wireless_ip);
        param(wireless, 4, {config_.security_type});
        param(response.payload, 1, wireless);
        const std::vector<std::uint8_t> port{
            0, 0, static_cast<std::uint8_t>(config_.airplay_port >> 8),
            static_cast<std::uint8_t>(config_.airplay_port)};
        param(response.payload, 2, port);
        textParam(response.payload, 3, config_.device_identifier);
        textParam(response.payload, 4, config_.public_key);
        textParam(response.payload, 5, config_.source_version);
        log_.info("wireless: sending CarPlayStartSession for SSID " + config_.ssid +
                  " via " + config_.wireless_ip + ":" + std::to_string(config_.airplay_port));
        return {response};
    }
    if (in.id == static_cast<MsgId>(0x4E0D)) {
        std::vector<std::uint8_t> availability;
        if (parameter0(in.payload, availability) && availability.size() == 1 &&
            availability[0] <= 1) {
            log_.info("wireless: CarPlay availability update=" +
                      std::to_string(availability[0]));
        } else {
            log_.warn("wireless: malformed CarPlay availability update");
        }
        return {};
    }
    if (in.id == static_cast<MsgId>(0x4E0E)) {
        transport_notified_ = true;
        log_.info("wireless: device transport identifier notification received");
        if (post_transport_config_sends_ >= 2) {
            log_.warn("wireless: post-transport Wi-Fi configuration send limit reached");
            return {};
        }
        ++post_transport_config_sends_;
        Message response;
        response.id = static_cast<MsgId>(0x5703);
        textParam(response.payload, 1, config_.ssid);
        textParam(response.payload, 2, config_.passphrase);
        param(response.payload, 3, {config_.security_type});
        param(response.payload, 4, {config_.channel});
        log_.info("wireless: sending post-transport accessory Wi-Fi configuration (" +
                  std::to_string(post_transport_config_sends_) + "/2)");
        return {response};
    }
    if (in.id == static_cast<MsgId>(0x5702)) {
        if (transport_notified_) {
            if (post_transport_config_sends_ >= 2) {
                log_.warn("wireless: post-transport Wi-Fi configuration send limit reached");
                return {};
            }
            ++post_transport_config_sends_;
        } else {
            if (pre_transport_config_sends_ >= 5) {
                log_.warn("wireless: pre-transport Wi-Fi configuration send limit reached");
                return {};
            }
            ++pre_transport_config_sends_;
        }
        Message response;
        response.id = static_cast<MsgId>(0x5703);
        textParam(response.payload, 1, config_.ssid);
        textParam(response.payload, 2, config_.passphrase);
        param(response.payload, 3, {config_.security_type});
        param(response.payload, 4, {config_.channel});
        log_.info("wireless: sending accessory Wi-Fi configuration for SSID " + config_.ssid);
        return {response};
    }
    if (in.id == static_cast<MsgId>(0x5701)) {
        std::vector<std::uint8_t> status;
        if (parameter0(in.payload, status) && !status.empty())
            log_.info("wireless: phone Wi-Fi status=" + std::to_string(status[0]));
        else
            log_.warn("wireless: malformed Wi-Fi status");
        return {};
    }
    return {};
}

}  // namespace iap2
