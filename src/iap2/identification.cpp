#include "iap2/identification.hpp"
#include <cstdio>
#include <stdexcept>

namespace iap2 {
namespace {
using Bytes = std::vector<std::uint8_t>;
void put16(Bytes& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}
void param(Bytes& body, std::uint16_t id, const Bytes& value) {
    put16(body, static_cast<std::uint16_t>(value.size() + 4));
    put16(body, id);
    body.insert(body.end(), value.begin(), value.end());
}
void u8(Bytes& body, std::uint16_t id, std::uint8_t value) { param(body, id, {value}); }
void u16(Bytes& body, std::uint16_t id, std::uint16_t value) {
    Bytes b; put16(b, value); param(body, id, b);
}
void text(Bytes& body, std::uint16_t id, const std::string& value) {
    Bytes b(value.begin(), value.end()); b.push_back(0); param(body, id, b);
}
void voidParam(Bytes& body, std::uint16_t id) { param(body, id, {}); }
void u16List(Bytes& body, std::uint16_t id, const std::vector<std::uint16_t>& values) {
    Bytes b; for (auto value : values) put16(b, value); param(body, id, b);
}
Bytes macBytes(const std::string& mac) {
    Bytes out; unsigned value = 0; int digits = 0;
    for (char c : mac) {
        if (c == ':') continue;
        unsigned nibble;
        if (c >= '0' && c <= '9') nibble = static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f') nibble = static_cast<unsigned>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') nibble = static_cast<unsigned>(c - 'A' + 10);
        else throw std::invalid_argument("invalid Bluetooth MAC address");
        value = (value << 4) | nibble;
        if (++digits % 2 == 0) { out.push_back(static_cast<std::uint8_t>(value)); value = 0; }
    }
    if (digits != 12 || out.size() != 6) throw std::invalid_argument("Bluetooth MAC must contain six bytes");
    return out;
}
std::string hexPayload(const Message& in) {
    std::string result; char byte[4];
    for (auto b : in.payload) { std::snprintf(byte, sizeof byte, "%02x ", b); result += byte; }
    return result;
}
Bytes identificationInformation(const IdentificationConfig& c) {
    Bytes body;
    text(body, 0, c.name); text(body, 1, c.model_identifier); text(body, 2, c.manufacturer);
    text(body, 3, c.serial_number); text(body, 4, c.firmware_version); text(body, 5, c.hardware_version);
    // Message lists match the decompiled DiPlay identification client.
    u16List(body, 6, {0xAA01, 0xAA03, 0x5000, 0x5002, 0x5200, 0x5203,
                      0xAE00, 0xAE02, 0x4157, 0x4159, 0x4154, 0x4156, 0x5703, 0x4301});
    u16List(body, 7, {0xAA00, 0xAA02, 0xAA04, 0xAA05, 0xEA00, 0xEA01, 0x5001,
                      0x5201, 0x5202, 0xAE01, 0x4158, 0x4155, 0x4300, 0x4E0D, 0x4E0E, 0x5702});
    u8(body, 8, 0); u16(body, 9, 20);

    Bytes group;
    u8(group, 0, 1); text(group, 1, c.external_accessory_protocol); u8(group, 2, 0);
    param(body, 10, group);
    text(body, 12, c.language);
    Bytes languages(c.language.begin(), c.language.end()); languages.push_back(0); param(body, 13, languages);

    group.clear();
    u16(group, 0, 0); text(group, 1, "blue"); voidParam(group, 2);
    param(group, 3, macBytes(c.bluetooth_mac)); text(group, 4, "blue"); voidParam(group, 5);
    param(body, 17, group);

    group.clear();
    u16(group, 0, 1); text(group, 1, c.ssid); voidParam(group, 2); u16(group, 3, 1);
    voidParam(group, 4); voidParam(group, 5);
    param(body, 24, group);

    group.clear();
    u16(group, 0, 42); text(group, 1, "RouteGuidance"); u16(group, 2, 64);
    u16(group, 4, 64); u16(group, 6, 8);
    param(body, 30, group);
    return body;
}
}  // namespace

Identification::Identification(log::ILogger& log, IdentificationConfig config)
    : log_(log), config_(std::move(config)) {}

std::vector<Message> Identification::startIdentification() {
    // The phone initiates identification with 0x1D00; the accessory replies with 0x1D01.
    return {};
}

std::vector<Message> Identification::onMessage(const Message& in) {
    char id[8]; std::snprintf(id, sizeof id, "%04x", static_cast<unsigned>(in.id));
    log_.info("ident: received message 0x" + std::string(id) +
              " (" + std::to_string(in.payload.size()) + " bytes): " + hexPayload(in));
    if (in.id == static_cast<MsgId>(0x1D00)) {
        Message response; response.id = static_cast<MsgId>(0x1D01);
        response.payload = identificationInformation(config_);
        log_.info("ident: sending IdentificationInformation");
        return {response};
    }
    if (in.id == static_cast<MsgId>(0x1D03)) {
        log_.error("ident: rejected parameters: " + hexPayload(in));
        return {};
    }
    if (in.id == static_cast<MsgId>(0x1D02)) {
        done_ = true;
        log_.info("ident: accepted");
        return {};
    }
    log_.warn("ident: unexpected message id");
    return {};
}

}  // namespace iap2
