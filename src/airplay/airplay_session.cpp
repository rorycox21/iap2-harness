#include "airplay/airplay_session.hpp"
#include "airplay/auth_setup.hpp"
#include "airplay/mac_video.hpp"
#include "airplay_crypto.h"
#include "airplay/tlv.hpp"
#include "transport/tcp_transport.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace bplist = fxchain::airplay::bplist;

namespace airplay {

AirPlaySession::AirPlaySession(transport::Transport& t, mfi::Signer& signer,
                               log::ILogger& log, PairingProtocol* pairing)
    : t_(t), signer_(signer), log_(log),
      owned_pairing_(pairing ? nullptr : std::make_unique<PairingProtocol>()),
      pairing_(pairing ? pairing : owned_pairing_.get()) {}

AirPlaySession::~AirPlaySession() {
    if (screen_stream_) screen_stream_->close();
    for (auto& stream : audio_streams_) stream.second->close();
    audio_streams_.clear();
    stopPcmAudioOutput();
    if (aux_channels_) aux_channels_->close();
}

static std::string find_header(const std::string& headers, const std::string& name) {
    std::string lower;
    lower.reserve(name.size());
    for (char c : name) lower.push_back(static_cast<char>(std::tolower(c)));
    std::string h = headers;
    // Simple scan for the header name at the start of a line.
    for (std::size_t i = 0; i < h.size();) {
        if ((i == 0 || h[i-1] == '\n') &&
            h.size() - i >= name.size()) {
            std::string tag = h.substr(i, name.size());
            for (auto& ch : tag) ch = static_cast<char>(std::tolower(ch));
            if (tag == lower) {
                auto colon = h.find(':', i);
                if (colon == std::string::npos) return {};
                auto end = h.find('\r', colon);
                if (end == std::string::npos) end = h.find('\n', colon);
                if (end == std::string::npos) end = h.size();
                auto start = colon + 1;
                while (start < end && (h[start] == ' ' || h[start] == '\t')) ++start;
                return h.substr(start, end - start);
            }
        }
        auto nl = h.find('\n', i);
        if (nl == std::string::npos) break;
        i = nl + 1;
    }
    return {};
}

void AirPlaySession::writeResponse(int status, const std::string& reason,
                                   const std::string& content_type,
                                   const std::vector<std::uint8_t>& body) {
    std::string head = "RTSP/1.0 " + std::to_string(status) + " " + reason + "\r\n";
    if (!content_type.empty())
        head += "Content-Type: " + content_type + "\r\n";
    head += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    head += "\r\n";
    std::vector<std::uint8_t> response(head.begin(), head.end());
    response.insert(response.end(), body.begin(), body.end());
    if (control_cipher_enabled_) response = encryptControlRecords(response);
    if (!response.empty()) t_.write(response.data(), response.size());
    log_.debug("airplay: sent " + std::to_string(status) + " " + reason +
               " body=" + std::to_string(body.size()) + " bytes");
}

bool AirPlaySession::decryptControlRecords() {
    constexpr std::size_t kHeaderBytes = 2;
    constexpr std::size_t kTagBytes = 16;
    constexpr std::size_t kMaxPayload = 16384;
    std::size_t consumed = 0;
    while (encrypted_recv_buf_.size() - consumed >= kHeaderBytes) {
        const auto* header = encrypted_recv_buf_.data() + consumed;
        const std::size_t payload_size = static_cast<std::size_t>(header[0]) |
            (static_cast<std::size_t>(header[1]) << 8);
        if (payload_size > kMaxPayload) {
            log_.error("airplay: encrypted control record exceeds 16 KiB");
            return false;
        }
        const std::size_t record_size = kHeaderBytes + payload_size + kTagBytes;
        if (encrypted_recv_buf_.size() - consumed < record_size) break;

        const fxchain::airplay::Bytes nonce =
            fxchain::airplay::counterNonce8(control_read_counter_);
        const fxchain::airplay::Bytes aad(header, header + kHeaderBytes);
        const fxchain::airplay::Bytes sealed(
            header + kHeaderBytes, header + record_size);
        const auto plaintext = fxchain::airplay::chacha20Poly1305Decrypt(
            control_read_key_, nonce, sealed, aad);
        if (!plaintext) {
            log_.error("airplay: encrypted control authentication failed");
            return false;
        }
        recv_buf_.append(plaintext->begin(), plaintext->end());
        ++control_read_counter_;
        consumed += record_size;
    }
    if (consumed != 0) {
        encrypted_recv_buf_.erase(encrypted_recv_buf_.begin(),
                                  encrypted_recv_buf_.begin() + consumed);
    }
    return true;
}

std::vector<std::uint8_t> AirPlaySession::encryptControlRecords(
    const std::vector<std::uint8_t>& plaintext) {
    constexpr std::size_t kMaxPayload = 16384;
    std::vector<std::uint8_t> encrypted;
    for (std::size_t offset = 0; offset < plaintext.size(); offset += kMaxPayload) {
        const std::size_t size = std::min(kMaxPayload, plaintext.size() - offset);
        const std::vector<std::uint8_t> header{
            static_cast<std::uint8_t>(size),
            static_cast<std::uint8_t>(size >> 8)};
        const fxchain::airplay::Bytes nonce =
            fxchain::airplay::counterNonce8(control_write_counter_);
        const fxchain::airplay::Bytes chunk(plaintext.begin() + offset,
                                             plaintext.begin() + offset + size);
        const auto sealed = fxchain::airplay::chacha20Poly1305Encrypt(
            control_write_key_, nonce, chunk, header);
        encrypted.insert(encrypted.end(), header.begin(), header.end());
        encrypted.insert(encrypted.end(), sealed.begin(), sealed.end());
        ++control_write_counter_;
    }
    return encrypted;
}

bool AirPlaySession::handleRequest(const std::string& method, const std::string& path,
                                   const std::string& headers,
                                   const std::vector<std::uint8_t>& body,
                                   bool& pairing_seen) {
    log_.info("airplay: " + method + " " + path +
              " (" + std::to_string(body.size()) + " body bytes)");
    if (!headers.empty()) log_.debug("airplay: headers:\n" + headers);

    if (method == "GET" && path.rfind("/info", 0) == 0) {
        // CarPlay capability fields and display nesting follow the local
        // DiPlay AirPlayInfoPlist source. Identity values are configurable;
        // advertisement and /info values must match for live discovery.
        auto envOr = [](const char* key, const char* fallback) {
            const char* value = std::getenv(key);
            return std::string(value && *value ? value : fallback);
        };
        const std::string device_id = envOr("CARPLAY_DEVICE_ID", "02:00:00:00:00:01");
        const std::string bluetooth_id = envOr("CARPLAY_BT_MAC", device_id.c_str());
        bplist::Dict d;
        d.push_back({"deviceID", bplist::Value::str(device_id)});
        d.push_back({"features", bplist::Value::integer(418060152546LL)});
        d.push_back({"statusFlags", bplist::Value::integer(4)});
        d.push_back({"model", bplist::Value::str(envOr("CARPLAY_MODEL", "MacCarPlay,1"))});
        d.push_back({"manufacturer", bplist::Value::str(envOr("CARPLAY_MANUFACTURER", "BMW"))});
        d.push_back({"sourceVersion", bplist::Value::str("950.7.1")});
        d.push_back({"name", bplist::Value::str(envOr("CARPLAY_NAME", "MacCarPlay"))});
        d.push_back({"bluetoothIDs", bplist::Value::array({bplist::Value::str(bluetooth_id)})});
        d.push_back({"rightHandDrive", bplist::Value::boolean(false)});
        d.push_back({"keepAliveLowPower", bplist::Value::boolean(false)});
        d.push_back({"keepAliveSendStatsAsBody", bplist::Value::boolean(false)});

        bplist::Dict area;
        area.push_back({"widthPixels", bplist::Value::integer(1024)});
        area.push_back({"heightPixels", bplist::Value::integer(600)});
        area.push_back({"originXPixels", bplist::Value::integer(0)});
        area.push_back({"originYPixels", bplist::Value::integer(0)});
        bplist::Dict safe_area = area;
        safe_area.push_back({"drawUIOutsideSafeArea", bplist::Value::boolean(true)});
        area.push_back({"safeArea", bplist::Value::object(std::move(safe_area))});
        bplist::Dict display;
        display.push_back({"uuid", bplist::Value::str("b7e6c5a0-1111-4000-8000-000000000001")});
        display.push_back({"type", bplist::Value::integer(110)});
        display.push_back({"maxFPS", bplist::Value::integer(60)});
        display.push_back({"widthPixels", bplist::Value::integer(1024)});
        display.push_back({"heightPixels", bplist::Value::integer(600)});
        display.push_back({"widthPhysical", bplist::Value::integer(300)});
        display.push_back({"heightPhysical", bplist::Value::integer(176)});
        display.push_back({"features", bplist::Value::integer(10)});
        display.push_back({"primaryInputDevice", bplist::Value::integer(1)});
        display.push_back({"uiAppearanceMode", bplist::Value::integer(0)});
        display.push_back({"uiAppearanceSetting", bplist::Value::integer(0)});
        display.push_back({"mapAppearanceMode", bplist::Value::integer(0)});
        display.push_back({"mapAppearanceSetting", bplist::Value::integer(0)});
        display.push_back({"viewAreas", bplist::Value::array({
            bplist::Value::object(area)})});
        display.push_back({"initialViewArea", bplist::Value::integer(0)});
        d.push_back({"displays", bplist::Value::array({
            bplist::Value::object(std::move(display))})});
        bplist::Dict resource_screen;
        resource_screen.push_back({"resourceID", bplist::Value::integer(1)});
        resource_screen.push_back({"transferType", bplist::Value::integer(1)});
        resource_screen.push_back({"transferPriority", bplist::Value::integer(100)});
        resource_screen.push_back({"takeConstraint", bplist::Value::integer(100)});
        resource_screen.push_back({"borrowConstraint", bplist::Value::integer(100)});
        resource_screen.push_back({"unborrowConstraint", bplist::Value::integer(100)});
        bplist::Dict resource_audio = resource_screen;
        resource_audio[0].second = bplist::Value::integer(2);
        bplist::Dict modes;
        modes.push_back({"resources", bplist::Value::array({
            bplist::Value::object(std::move(resource_screen)),
            bplist::Value::object(std::move(resource_audio))})});
        bplist::Dict speech_state;
        speech_state.push_back({"appStateID", bplist::Value::integer(1)});
        speech_state.push_back({"speechMode", bplist::Value::integer(-1)});
        bplist::Dict audio_state;
        audio_state.push_back({"appStateID", bplist::Value::integer(2)});
        audio_state.push_back({"state", bplist::Value::boolean(false)});
        bplist::Dict phone_state;
        phone_state.push_back({"appStateID", bplist::Value::integer(3)});
        phone_state.push_back({"state", bplist::Value::boolean(false)});
        modes.push_back({"appStates", bplist::Value::array({
            bplist::Value::object(std::move(audio_state)),
            bplist::Value::object(std::move(speech_state)),
            bplist::Value::object(std::move(phone_state))})});
        d.push_back({"modes", bplist::Value::object(std::move(modes))});
        bplist::Array audio_latencies;
        auto add_audio_latency = [&audio_latencies](int type, const char* audio_type) {
            bplist::Dict latency;
            latency.push_back({"type", bplist::Value::integer(type)});
            latency.push_back({"inputLatencyMicros", bplist::Value::integer(0)});
            latency.push_back({"outputLatencyMicros", bplist::Value::integer(0)});
            if (audio_type) latency.push_back({"audioType", bplist::Value::str(audio_type)});
            audio_latencies.push_back(bplist::Value::object(std::move(latency)));
        };
        add_audio_latency(100, nullptr);
        add_audio_latency(100, "default");
        add_audio_latency(100, "media");
        add_audio_latency(100, "telephony");
        add_audio_latency(100, "speechRecognition");
        add_audio_latency(100, "alert");
        add_audio_latency(101, nullptr);
        add_audio_latency(101, "default");
        add_audio_latency(102, "default");
        d.push_back({"audioLatencies", bplist::Value::array(std::move(audio_latencies))});
        bplist::Dict pcm_audio;
        pcm_audio.push_back({"type", bplist::Value::integer(100)});
        pcm_audio.push_back({"audioType", bplist::Value::str("media")});
        pcm_audio.push_back({"audioOutputFormats", bplist::Value::integer(4092)});
        d.push_back({"audioFormats", bplist::Value::array({
            bplist::Value::object(std::move(pcm_audio))})});
        d.push_back({"extendedFeatures", bplist::Value::array({
            bplist::Value::str("vocoderInfo"),
            bplist::Value::str("enhancedRequestCarUI")})});
        auto hid_entry = [](std::uint32_t uid, const char* name,
                            std::vector<std::uint8_t> descriptor) {
            char uuid[16];
            std::snprintf(uuid, sizeof uuid, "%x", uid);
            bplist::Dict hid;
            hid.push_back({"hidProductID", bplist::Value::integer(1)});
            hid.push_back({"hidVendorID", bplist::Value::integer(2)});
            hid.push_back({"hidCountryCode", bplist::Value::integer(0)});
            hid.push_back({"uuid", bplist::Value::str(uuid)});
            hid.push_back({"name", bplist::Value::str(name)});
            hid.push_back({"displayUUID", bplist::Value::str(
                "b7e6c5a0-1111-4000-8000-000000000001")});
            hid.push_back({"hidDescriptor", bplist::Value::bytes(std::move(descriptor))});
            return bplist::Value::object(std::move(hid));
        };
        std::vector<std::uint8_t> touch_descriptor{5, 13, 9, 4, 161, 1};
        auto append_finger = [&touch_descriptor](std::uint16_t x, std::uint16_t y) {
            const std::uint8_t bytes[] = {
                5,13,9,34,161,2,9,56,117,8,149,1,129,2,21,0,37,1,9,51,117,1,
                149,1,129,2,149,7,129,3,5,1,38,
                static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(x >> 8),
                9,48,117,16,149,1,129,2,38,
                static_cast<std::uint8_t>(y), static_cast<std::uint8_t>(y >> 8),
                9,49,129,2,192};
            touch_descriptor.insert(touch_descriptor.end(), std::begin(bytes), std::end(bytes));
        };
        append_finger(1024, 600);
        append_finger(1024, 600);
        touch_descriptor.push_back(192);
        const std::vector<std::uint8_t> knob_descriptor{
            5,1,9,8,161,1,5,9,9,1,21,0,37,1,117,1,149,1,129,2,5,12,10,35,
            2,10,36,2,149,2,129,2,149,5,129,1,5,1,9,1,161,0,9,48,9,49,
            21,129,37,127,117,8,149,2,129,2,192,9,56,21,129,37,127,117,8,
            149,1,129,6,192};
        const std::vector<std::uint8_t> media_descriptor{
            5,12,9,1,161,1,21,0,37,6,5,12,10,0,0,10,176,0,10,177,0,10,205,
            0,10,181,0,10,182,0,10,158,2,117,8,149,1,129,0,192};
        const std::vector<std::uint8_t> telephony_descriptor{
            5,11,9,7,161,1,21,0,37,17,5,11,9,0,9,32,9,33,9,38,9,47,9,176,
            9,177,9,178,9,179,9,180,9,181,9,182,9,183,9,184,9,185,9,186,
            9,187,5,7,9,42,117,8,149,1,129,0,192};
        d.push_back({"hidDevices", bplist::Value::array({
            hid_entry(707406378, "iap2-harness Touchscreen", std::move(touch_descriptor)),
            hid_entry(707406379, "iap2-harness Knob", knob_descriptor),
            hid_entry(707406380, "iap2-harness Media", media_descriptor),
            hid_entry(707406381, "iap2-harness Telephony", telephony_descriptor)})});
        auto wire = bplist::encode(bplist::Value::object(std::move(d)));
        writeResponse(200, "OK", "application/x-apple-binary-plist", wire);
        return true;
    }

    if (method == "POST" && path.rfind("/pair-setup", 0) == 0) {
        pairing_seen = true;
        const auto response = pairing_->handlePairSetup(body);
        log_.info(std::string("airplay: pair-setup state response; complete=") +
                  (pairing_->pairSetupComplete() ? "yes" : "no"));
        writeResponse(200, "OK", "application/octet-stream", response);
        return true;
    }

    if (method == "POST" && path.rfind("/pair-verify", 0) == 0) {
        pairing_seen = true;
        pair_verify_request_seen_ = true;
        const auto response = pairing_->handlePairVerify(body);
        log_.info(std::string("airplay: pair-verify state response; verified=") +
                  (pairing_->pairVerified() ? "yes" : "no"));
        writeResponse(200, "OK", "application/octet-stream", response);
        return true;
    }

    if (method == "POST" && path == "/auth-setup") {
        if (!control_cipher_enabled_ || !pairing_->pairVerified()) {
            writeResponse(403, "Forbidden", "", {});
            return true;
        }
        const auto response = mfiAuthSetup(body, signer_);
        if (!response) {
            log_.warn("airplay: MFi auth-setup request/signing failed");
            writeResponse(400, "Bad Request", "", {});
            return true;
        }
        log_.info("airplay: MFi auth-setup response generated");
        writeResponse(200, "OK", "application/octet-stream", *response);
        return true;
    }

    if (method == "OPTIONS") {
        std::vector<std::uint8_t> empty;
        writeResponse(200, "OK", "", empty);
        return true;
    }

    if (method == "POST" && path.size() >= 8 &&
        path.compare(path.size() - 8, 8, "/command") == 0) {
        // DiPlay acknowledges commands with an empty RTSP 200 response. The
        // Mac display currently has no remote touch/knob report path, so log
        // the command and keep the control session alive.
        std::string command = "<unparsed>";
        const auto decoded = bplist::decode(body);
        if (decoded && decoded->type == bplist::Value::Type::Dict) {
            const auto* type = decoded->find("type");
            if (type && type->type == bplist::Value::Type::Str) command = type->s;
        }
        log_.info("airplay: command " + command);
        writeResponse(200, "OK", "", {});
        return true;
    }

    if (method == "POST" && path.size() >= 9 &&
        path.compare(path.size() - 9, 9, "/feedback") == 0) {
        log_.info("airplay: feedback received (" + std::to_string(body.size()) + " bytes)");
        writeResponse(200, "OK", "application/x-apple-binary-plist",
                      bplist::encode(bplist::Value::object({})));
        return true;
    }

    if (method == "SETUP") {
        const auto request = bplist::decode(body);
        if (!request || request->type != bplist::Value::Type::Dict) {
            writeResponse(400, "Bad Request", "", {});
            return true;
        }
        const auto* streams = request->find("streams");
        if (!streams) {
            if (pairing_->sharedSecret().empty()) {
                writeResponse(403, "Forbidden", "", {});
                return true;
            }
            if (!aux_channels_) {
                aux_channels_ = std::make_shared<AirPlayAuxChannels>(pairing_->sharedSecret());
                if (!aux_channels_->start()) {
                    aux_channels_.reset();
                    writeResponse(500, "Internal Server Error", "", {});
                    return true;
                }
                std::weak_ptr<AirPlayAuxChannels> weak_channels = aux_channels_;
                setVideoTouchHandler([weak_channels](bool down, std::uint16_t x,
                                                     std::uint16_t y) {
                    if (const auto channels = weak_channels.lock())
                        (void)channels->sendTouch(down, x, y);
                });
            }
            bplist::Dict response_dict;
            response_dict.push_back({"timingPort",
                bplist::Value::integer(aux_channels_->timingPort())});
            response_dict.push_back({"eventPort",
                bplist::Value::integer(aux_channels_->eventPort())});
            response_dict.push_back({"enabledFeatures", bplist::Value::array({
                bplist::Value::str("iAPChannel"),
                bplist::Value::str("viewAreas")})});
            const auto response = bplist::encode(bplist::Value::object(std::move(response_dict)));
            writeResponse(200, "OK", "application/x-apple-binary-plist", response);
            return true;
        }
        if (streams->type != bplist::Value::Type::Arr) {
            writeResponse(400, "Bad Request", "", {});
            return true;
        }

        bplist::Array accepted;
        for (const auto& stream : streams->arr) {
            if (stream.type != bplist::Value::Type::Dict) continue;
            const auto* type = stream.find("type");
            if (!type || type->type != bplist::Value::Type::Int) continue;
            const auto* connection_id = stream.find("streamConnectionID");
            if (!connection_id || connection_id->type != bplist::Value::Type::Int ||
                pairing_->sharedSecret().empty()) continue;

            if (type->i == 100) {
                const auto* audio_type = stream.find("audioType");
                if (!audio_type || audio_type->type != bplist::Value::Type::Str ||
                    audio_type->s != "media") continue;
                const auto* audio_format = stream.find("audioFormat");
                if (!audio_format || audio_format->type != bplist::Value::Type::Int) continue;
                const std::uint64_t connection_value =
                    static_cast<std::uint64_t>(connection_id->i);
                const auto previous = audio_streams_.find(connection_value);
                if (previous != audio_streams_.end()) {
                    previous->second->close();
                    audio_streams_.erase(previous);
                }
                std::uint32_t sample_rate = 0;
                std::uint8_t channels = 0;
                switch (static_cast<std::uint64_t>(audio_format->i)) {
                case 4: sample_rate = 8000; channels = 1; break;
                case 8: sample_rate = 8000; channels = 2; break;
                case 16: sample_rate = 16000; channels = 1; break;
                case 32: sample_rate = 16000; channels = 2; break;
                case 64: sample_rate = 24000; channels = 1; break;
                case 128: sample_rate = 24000; channels = 2; break;
                case 256: sample_rate = 32000; channels = 1; break;
                case 512: sample_rate = 32000; channels = 2; break;
                case 1024: sample_rate = 44100; channels = 1; break;
                case 2048: sample_rate = 44100; channels = 2; break;
                default: continue;
                }
                if (!configurePcmAudioOutput(sample_rate, channels)) {
                    log_.error("airplay: could not configure Mac PCM audio output");
                    continue;
                }
                const auto connection_text = std::to_string(connection_value);
                const auto key = fxchain::airplay::hkdfSha512(
                    "DataStream-Salt" + connection_text,
                    "DataStream-Output-Encryption-Key", pairing_->sharedSecret());
                const auto* tcp = dynamic_cast<const transport::TcpTransport*>(&t_);
                const std::string expected_peer = tcp ? tcp->peerAddress() : std::string{};
                auto packet_count = std::make_shared<std::atomic<std::uint64_t>>(0);
                auto audio = std::make_unique<AudioStreamReceiver>(
                    key, expected_peer,
                    [this, packet_count, connection_text](const auto& rtp) {
                        submitPcmAudioRtp(rtp);
                        const auto count = ++(*packet_count);
                        if (count == 1 || count % 100 == 0)
                            log_.info("airplay: decrypted audio RTP stream=" +
                                      connection_text + " packets=" +
                                      std::to_string(count) + " bytes=" +
                                      std::to_string(rtp.size()));
                    });
                const auto ports = audio->start();
                if (ports.first == 0 || ports.second == 0) {
                    log_.error("airplay: could not start encrypted audio RTP receiver");
                    continue;
                }
                audio_streams_[connection_value] = std::move(audio);
                bplist::Dict response_stream;
                response_stream.push_back({"type", bplist::Value::integer(100)});
                response_stream.push_back({"dataPort", bplist::Value::integer(ports.first)});
                response_stream.push_back({"controlPort", bplist::Value::integer(ports.second)});
                response_stream.push_back({"streamConnectionID",
                                           bplist::Value::integer(connection_id->i)});
                accepted.push_back(bplist::Value::object(std::move(response_stream)));
                log_.info("airplay: encrypted audio receiver listening data=" +
                          std::to_string(ports.first) + " control=" +
                          std::to_string(ports.second));
                continue;
            }

            if (type->i != 110) continue;
            const auto connection_text = std::to_string(
                static_cast<std::uint64_t>(connection_id->i));
            const auto key = fxchain::airplay::hkdfSha512(
                "DataStream-Salt" + connection_text,
                "DataStream-Output-Encryption-Key", pairing_->sharedSecret());
            const auto* tcp = dynamic_cast<const transport::TcpTransport*>(&t_);
            const std::string expected_peer = tcp ? tcp->peerAddress() : std::string{};
            screen_stream_.reset();
            screen_frame_count_ = 0;
            screen_stream_ = std::make_unique<ScreenStreamReceiver>(
                key, expected_peer,
                [this](const std::string& codec, const fxchain::airplay::Bytes& config) {
                    log_.info("airplay: screen codec config " + codec + " (" +
                              std::to_string(config.size()) + " bytes)");
                    configureVideoDisplay(codec, config);
                },
                [this](const fxchain::airplay::Bytes& frame, std::uint64_t,
                       std::uint64_t) {
                    submitVideoFrame(frame);
                    ++screen_frame_count_;
                    if (screen_frame_count_ == 1 || screen_frame_count_ % 300 == 0)
                        log_.info("airplay: decrypted screen frame " +
                                  std::to_string(screen_frame_count_) + " (" +
                                  std::to_string(frame.size()) + " bytes)");
                });
            const auto data_port = screen_stream_->start();
            if (data_port == 0) {
                screen_stream_.reset();
                continue;
            }
            bplist::Dict response_stream;
            response_stream.push_back({"type", bplist::Value::integer(110)});
            response_stream.push_back({"dataPort", bplist::Value::integer(data_port)});
            accepted.push_back(bplist::Value::object(std::move(response_stream)));
        }

        bplist::Dict response_dict;
        response_dict.push_back({"streams", bplist::Value::array(std::move(accepted))});
        const auto response = bplist::encode(bplist::Value::object(std::move(response_dict)));
        writeResponse(200, "OK", "application/x-apple-binary-plist", response);
        return true;
    }

    log_.warn("airplay: unsupported method " + method + " " + path);
    std::vector<std::uint8_t> empty;
    writeResponse(501, "Not Implemented", "", empty);
    return true;
}

bool AirPlaySession::run() {
    bool pairing_seen = false;
    std::vector<std::uint8_t> tmp(8192);
    while (true) {
        // Try to parse one complete RTSP message from recv_buf_.
        // RTSP messages end with \r\n\r\n + Content-Length bytes.
        auto hdr_end = recv_buf_.find("\r\n\r\n");
        if (hdr_end == std::string::npos) {
            int n = t_.read(tmp.data(), tmp.size(), 5000);
            if (n <= 0) {
                if (n == 0) continue;  // timeout, try again
                log_.warn("airplay: read error or peer closed");
                return pairing_seen;
            }
            if (control_cipher_enabled_) {
                encrypted_recv_buf_.insert(encrypted_recv_buf_.end(), tmp.begin(),
                                           tmp.begin() + n);
                if (!decryptControlRecords()) return pairing_seen;
            } else {
                recv_buf_.append(reinterpret_cast<const char*>(tmp.data()), n);
            }
            continue;
        }

        // Parse request line and headers.
        std::string head = recv_buf_.substr(0, hdr_end);
        auto line_end = head.find("\r\n");
        std::string request_line = head.substr(0, line_end);
        std::string headers = (line_end == std::string::npos)
                              ? "" : head.substr(line_end + 2);

        auto sp1 = request_line.find(' ');
        auto sp2 = request_line.rfind(' ');
        if (sp1 == std::string::npos || sp2 == std::string::npos) {
            log_.warn("airplay: malformed request line: " + request_line);
            return false;
        }
        std::string method = request_line.substr(0, sp1);
        std::string path   = request_line.substr(sp1 + 1, sp2 - sp1 - 1);

        // Body length from Content-Length header.
        std::size_t body_len = 0;
        auto cl = find_header(headers, "Content-Length");
        if (!cl.empty()) body_len = static_cast<std::size_t>(std::stoul(cl));

        std::size_t total = hdr_end + 4 + body_len;
        if (recv_buf_.size() < total) {
            int n = t_.read(tmp.data(), tmp.size(), 5000);
            if (n <= 0) {
                if (n == 0) continue;
                log_.warn("airplay: read error while reading body");
                return pairing_seen;
            }
            if (control_cipher_enabled_) {
                encrypted_recv_buf_.insert(encrypted_recv_buf_.end(), tmp.begin(),
                                           tmp.begin() + n);
                if (!decryptControlRecords()) return pairing_seen;
            } else {
                recv_buf_.append(reinterpret_cast<const char*>(tmp.data()), n);
            }
            continue;
        }

        std::vector<std::uint8_t> body;
        if (body_len > 0) {
            body.assign(recv_buf_.begin() + hdr_end + 4,
                        recv_buf_.begin() + total);
        }
        recv_buf_.erase(0, total);

        if (!handleRequest(method, path, headers, body, pairing_seen))
            return pairing_seen;

        // Pair-verify's state-4 response is the final plaintext response.
        // Switch only after it has been written, then decrypt any encrypted
        // record bytes coalesced into the same TCP read.
        if (!control_cipher_enabled_ && pair_verify_request_seen_ &&
            pairing_->pairVerified()) {
            // The local reference names these keys from the controller's
            // perspective: its cipher reads with Control-Write and writes
            // with Control-Read.
            control_read_key_ = pairing_->controlWriteKey();
            control_write_key_ = pairing_->controlReadKey();
            if (control_read_key_.size() != 32 || control_write_key_.size() != 32) {
                log_.error("airplay: pair-verify completed without 32-byte control keys");
                return pairing_seen;
            }
            control_cipher_enabled_ = true;
            if (!recv_buf_.empty()) {
                encrypted_recv_buf_.insert(encrypted_recv_buf_.end(),
                    recv_buf_.begin(), recv_buf_.end());
                recv_buf_.clear();
                if (!decryptControlRecords()) return pairing_seen;
            }
            log_.info("airplay: encrypted RTSP control enabled");
        }
    }
}

}  // namespace airplay
