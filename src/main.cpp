// iap2-harness: drives the iAP2 authentication session.
//
//   iap2-harness --simulate
//       Runs AuthSession against MockSigner with a scripted message
//       sequence and prints the wire bytes. No USB, no real signer.
//
//   iap2-harness --list
//       Enumerates USB devices. Requires Stage 3 (libusb backend).
//
//   iap2-harness --vid 0x1234 --pid 0x5678 [--interface 0]
//                 [--ep-in 0x81] [--ep-out 0x02]
//       Opens the transport and runs the link loop. Requires Stage 3.
#include "iap2/auth_session.hpp"
#include "iap2/message.hpp"
#include "log/logger.hpp"
#include "mfi/mock_signer.hpp"
#include "mfi/openssl_signer.hpp"
#include "transport/transport.hpp"
#include "transport/tcp_transport.hpp"
#include "transport/bt_bridge_transport.hpp"
#include "transport/pipe_transport.hpp"
#include "airplay/airplay_session.hpp"
#include "airplay/mac_video.hpp"
#include "iap2/link_handshake.hpp"
#include "iap2/identification.hpp"
#include "iap2/control_messages.hpp"
#include "iap2/wireless_control.hpp"
#include "iap2/iap2_link.hpp"

#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>
#include <string>
#include <vector>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <filesystem>
#include <future>
#include <chrono>

static std::string hex(const std::vector<std::uint8_t>& v, std::size_t max = 64) {
    std::string s;
    const std::size_t n = v.size() < max ? v.size() : max;
    for (std::size_t i = 0; i < n; ++i) {
        char b[4];
        std::snprintf(b, sizeof b, "%02x ", v[i]);
        s += b;
    }
    if (v.size() > max) s += "...";
    return s;
}

static void printOut(const char* label, const std::vector<iap2::Message>& msgs) {
    std::printf("  %s: %zu message(s)\n", label, msgs.size());
    for (const auto& m : msgs) {
        auto wire = m.encode();
        std::printf("    [0x%04X len=%zu] %s\n",
                    static_cast<unsigned>(m.id), m.payload.size(), hex(wire).c_str());
    }
}

static int run_simulate() {
    mfi::MockSigner signer;
    log::StderrLogger log;
    iap2::AuthSession session(signer, log);

    std::printf("=== simulate: full happy-path authentication ===\n");

    iap2::Message m1; m1.id = iap2::MsgId::RequestAuthenticationCert;
    std::printf("phone -> %s\n", "RequestAuthenticationCertificate");
    auto out = session.onMessage(m1);
    printOut("accessory", out);

    iap2::Message m2; m2.id = iap2::MsgId::RequestAuthChallengeResponse;
    m2.payload = {0x00, 0x0c, 0x00, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    std::printf("phone -> %s (nonce %s)\n",
                "RequestAuthenticationChallengeResponse", hex(m2.payload).c_str());
    out = session.onMessage(m2);
    printOut("accessory", out);

    if (out.size() != 1 || out[0].id != iap2::MsgId::AuthenticationResponse ||
        session.state() != iap2::AuthState::ChallengeRequested) {
        std::fprintf(stderr, "simulation failed: no authentication response\n");
        return 2;
    }

    iap2::Message m3; m3.id = iap2::MsgId::AuthenticationSucceeded;
    std::printf("phone -> %s\n", "AuthenticationSucceeded");
    session.onMessage(m3);

    std::printf("final state: %s\n",
        session.state() == iap2::AuthState::Succeeded ? "Succeeded" : "other");
    return session.state() == iap2::AuthState::Succeeded ? 0 : 2;
}

static void usage() {
    std::printf(
        "usage:\n"
        "  iap2-harness --auth-only --address PHONE --mfi-cert FILE --mfi-key FILE [--bt-bridge PATH]\n"
        "  iap2-harness --simulate\n"
        "  iap2-harness --pipe\n"
        "  iap2-harness --capture --address PHONE [--bt-bridge PATH]\n"
        "  iap2-harness --address PHONE --mfi-cert FILE --mfi-key FILE [--bt-bridge PATH] [--bt-mac MAC] [--ssid NAME] [--wifi-channel N] [--wifi-security 0..4]\n"
        "  iap2-harness --receiver --address PHONE --mfi-cert FILE --mfi-key FILE --ssid NAME --wifi-ip ADDRESS [--bt-bridge PATH] [--bt-mac MAC] [--bind ADDRESS] [--port N]\n"
        "  iap2-harness --vid 0xV --pid 0xP [--interface N] [--ep-in H] [--ep-out H]\n"
        "  iap2-harness --list\n"
        "  iap2-harness --vid 0xV --pid 0xP [--interface N] [--ep-in H] [--ep-out H]\n");
}


#include <cstdlib>
#include <string>

static void run_airplay_server(const std::string& bind_addr, std::uint16_t port,
                               std::shared_ptr<airplay::PairingProtocol> pairing,
                               std::shared_ptr<std::promise<bool>> readiness = {},
                               std::shared_ptr<mfi::OpenSslSigner> mfi_signer = {}) {
    mfi::MockSigner mock_signer;
    mfi::Signer& signer = mfi_signer ? static_cast<mfi::Signer&>(*mfi_signer) : static_cast<mfi::Signer&>(mock_signer);
    log::StderrLogger log;
    bool first_listener = true;
    for (;;) {
        transport::TcpTransport tcp;
        if (!tcp.listen(bind_addr, port, 60000, [&] {
                if (first_listener && readiness) readiness->set_value(true);
                first_listener = false;
            })) {
            if (first_listener && readiness) readiness->set_value(false);
            std::fprintf(stderr, "wireless: listen failed\n");
            return;
        }
        airplay::AirPlaySession session(tcp, signer, log, pairing.get());
        const bool paired = session.run();
        tcp.close();
        std::fprintf(stderr, "wireless: connection ended, pairing_seen=%d; listening again\n",
                     paired);
    }
}

static int run_wireless(const std::string& bind_addr, std::uint16_t port,
                        std::shared_ptr<airplay::PairingProtocol> pairing) {
    return airplay::runVideoDisplayApplication([bind_addr, port, pairing] {
        run_airplay_server(bind_addr, port, pairing);
    });
}

static pid_t start_advertisement(const char* argv0, std::uint16_t port) {
    std::filesystem::path script = std::filesystem::current_path() / "run-advertise.sh";
    if (!std::filesystem::exists(script)) {
        std::filesystem::path executable(argv0 ? argv0 : "");
        if (executable.has_parent_path())
            script = std::filesystem::absolute(executable).parent_path().parent_path() /
                     "run-advertise.sh";
    }
    if (!std::filesystem::exists(script)) {
        std::fprintf(stderr, "mDNS advertisement script not found; start run-advertise.sh manually\n");
        return -1;
    }
    const std::string port_text = std::to_string(port);
    ::setenv("CARPLAY_AIRPLAY_PORT", port_text.c_str(), 1);
    const pid_t child = ::fork();
    if (child < 0) {
        std::perror("fork advertisement");
        return -1;
    }
    if (child == 0) {
        ::execl("/bin/bash", "bash", script.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    return child;
}

static void stop_advertisement(pid_t child) {
    if (child <= 0) return;
    ::kill(child, SIGTERM);
    int status = 0;
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {}
}

static int run_live_iap2(transport::UsbConfig cfg, std::string phone_address,
                         std::string bt_bridge_path, std::string bind_addr,
                         std::uint16_t port,
                         std::shared_ptr<mfi::OpenSslSigner> signer,
                         std::shared_ptr<airplay::PairingProtocol> pairing,
                         iap2::IdentificationConfig ident_cfg,
                         iap2::WirelessControlConfig wifi_cfg,
                         bool start_airplay_after_wifi, bool auth_only = false) {
    // AirPlay must be available independently of a new Bluetooth handoff:
    // a phone that already accepted CarPlay can reconnect directly over Wi-Fi.
    if (start_airplay_after_wifi) {
        auto readiness = std::make_shared<std::promise<bool>>();
        auto ready = readiness->get_future();
        std::thread(run_airplay_server, bind_addr, port, pairing, readiness, signer).detach();
        if (ready.wait_for(std::chrono::seconds(5)) != std::future_status::ready || !ready.get()) {
            log::StderrLogger startup_log;
            startup_log.error("receiver: AirPlay listener could not start");
            return 3;
        }
    }
    std::unique_ptr<transport::Transport> t;
    if (!phone_address.empty()) {
        if (bt_bridge_path.empty()) {
            const char* user_home = std::getenv("HOME");
            bt_bridge_path = std::string(user_home ? user_home : "") +
                "/Desktop/Projects/playport/macos/bt-bridge/bt-bridge";
        }
        t = std::make_unique<transport::BtBridgeTransport>(bt_bridge_path, phone_address);
    } else {
        t = transport::make_libusb(cfg);
    }
    if (!t) {
        std::fprintf(stderr, "no transport; use --simulate\n");
        return 3;
    }
    if (!t->open()) {
        std::fprintf(stderr, "transport open failed\n");
        return 3;
    }

    log::StderrLogger log;
    iap2::AuthSession session(*signer, log);
    iap2::Identification identification(log, ident_cfg);
    if (const char* passphrase = std::getenv("CARPLAY_WIFI_PASSPHRASE"))
        wifi_cfg.passphrase = passphrase;
    iap2::WirelessControl wireless_control(log, wifi_cfg);
    iap2::LinkHandshake handshake(*t, log);
    if (!handshake.perform()) {
        std::fprintf(stderr, "handshake failed\n");
        t->close();
        return 3;
    }
    iap2::Iap2LinkDecoder decoder;
    bool airplay_started = start_airplay_after_wifi;
    bool subscriptions_sent = false;

    std::fprintf(stderr, "link loop started; waiting for messages...\n");
    std::uint8_t buf[64 * 1024];
    std::uint8_t tx_sequence = 0x64;
    std::uint64_t acknowledged_packets = 0;
    bool pending_consumed = false;
    for (;;) {
        int got;
        if (!handshake.pendingBytes().empty() && !pending_consumed) {
            const auto& pending = handshake.pendingBytes();
            std::copy(pending.begin(), pending.end(), buf);
            got = static_cast<int>(pending.size());
            pending_consumed = true;
        } else {
            got = t->read(buf, sizeof buf, 5000);
        }
        if (got < 0) { std::fprintf(stderr, "read error\n"); break; }
        if (got == 0) continue;

        auto msgs = decoder.feed(buf, static_cast<std::size_t>(got));
        if (decoder.dataPacketCount() != acknowledged_packets) {
            auto ack = iap2::Iap2LinkEncoder::acknowledgement(
                static_cast<std::uint8_t>(tx_sequence - 1), decoder.lastReceivedSequence());
            if (t->write(ack.data(), ack.size()) < 0) {
                std::fprintf(stderr, "ACK write error\n");
                break;
            }
            acknowledged_packets = decoder.dataPacketCount();
        }
        for (const auto& m : msgs) {
            std::fprintf(stderr, "rx: 0x%04X len=%zu\n",
                         static_cast<unsigned>(m.id), m.payload.size());
            const bool carplay_availability = m.id == static_cast<iap2::MsgId>(0x4300);
            std::vector<iap2::Message> out;
            if (m.id == static_cast<iap2::MsgId>(0x1D00) ||
                m.id == static_cast<iap2::MsgId>(0x1D02) ||
                m.id == static_cast<iap2::MsgId>(0x1D03))
                out = identification.onMessage(m);
            else if (m.id == static_cast<iap2::MsgId>(0x4300) &&
                     start_airplay_after_wifi &&
                     session.state() != iap2::AuthState::Succeeded) {
                log.warn("handoff: ignoring CarPlay availability before iAP2 MFi authentication");
            }
            else if (auth_only && (m.id == static_cast<iap2::MsgId>(0x4300) ||
                                  m.id == static_cast<iap2::MsgId>(0x5701) ||
                                  m.id == static_cast<iap2::MsgId>(0x5702))) {
                log.info("auth-only: deferring Wi-Fi handoff");
            }
            else if (m.id == static_cast<iap2::MsgId>(0x4300) ||
                     m.id == static_cast<iap2::MsgId>(0x4E0D) ||
                     m.id == static_cast<iap2::MsgId>(0x4E0E) ||
                     m.id == static_cast<iap2::MsgId>(0x5701) ||
                     m.id == static_cast<iap2::MsgId>(0x5702))
                out = wireless_control.onMessage(m);
            else
                out = session.onMessage(m);

            if (auth_only && session.state() == iap2::AuthState::Succeeded) {
                log.info("AUTH SUCCEEDED; authentication-only probe complete");
                t->close();
                return 0;
            }
            if (auth_only && (m.id == static_cast<iap2::MsgId>(0x4300) ||
                              m.id == static_cast<iap2::MsgId>(0x5702))) out.clear();
            if (session.state() == iap2::AuthState::Succeeded && identification.done() && !subscriptions_sent) {
                auto subscriptions = iap2::controlSubscriptions();
                out.insert(out.end(), subscriptions.begin(), subscriptions.end());
                subscriptions_sent = true;
                log.info("iap2: sending post-authentication control subscriptions");
            }

            if (start_airplay_after_wifi && carplay_availability && !out.empty() &&
                session.state() == iap2::AuthState::Succeeded && !airplay_started) {
                auto readiness = std::make_shared<std::promise<bool>>();
                auto ready = readiness->get_future();
                std::thread(run_airplay_server, bind_addr, port, pairing, readiness, signer).detach();
                if (ready.wait_for(std::chrono::seconds(5)) != std::future_status::ready ||
                    !ready.get()) {
                    std::fprintf(stderr, "handoff: AirPlay listener failed to bind; not sending start-session\n");
                    out.clear();
                } else {
                    airplay_started = true;
                }
            }

            bool response_sent = true;
            for (const auto& r : out) {
                auto frame = iap2::Iap2LinkEncoder::encode(
                    r, tx_sequence++, decoder.lastReceivedSequence());
                if (frame.empty()) {
                    std::fprintf(stderr, "tx: frame too large, dropping\n");
                    response_sent = false;
                    continue;
                }
                std::fprintf(stderr, "tx: 0x%04X len=%zu frame=%zu bytes\n",
                             static_cast<unsigned>(r.id), r.payload.size(), frame.size());
                if (t->write(frame.data(), frame.size()) < 0) {
                    std::fprintf(stderr, "write error\n");
                    response_sent = false;
                    break;
                }
            }
            if (session.state() == iap2::AuthState::Failed) {
                std::fprintf(stderr, "AUTH FAILED, status=%u\n",
                             static_cast<unsigned>(session.lastFailStatus()));
                goto done;
            }
        }
    }
done:
    t->close();
    return session.state() == iap2::AuthState::Succeeded ? 0 : 4;
}

static int run_pipe() {
    transport::PipeTransport pipe;
    if (!pipe.open()) {
        std::fprintf(stderr, "pipe transport open failed\n");
        return 3;
    }

    mfi::MockSigner signer;
    log::StderrLogger log;
    iap2::AuthSession session(signer, log);
    iap2::LinkHandshake handshake(pipe, log);
    std::fprintf(stderr, "pipe: sending iAP2 link handshake\n");
    if (!handshake.sendSynOnly()) {
        std::fprintf(stderr, "pipe: failed to send link marker/SYN\n");
        pipe.close();
        return 3;
    }

    std::fprintf(stderr, "pipe: logging received bytes (hex); Ctrl-C to stop\n");
    std::uint8_t buf[4096];
    for (;;) {
        const int got = pipe.read(buf, sizeof buf, 0);
        if (got < 0) {
            std::fprintf(stderr, "pipe: read error\n");
            pipe.close();
            return 4;
        }
        if (got == 0) break;
        for (int i = 0; i < got; ++i) {
            std::fprintf(stderr, "%02x%s", buf[i], (i + 1) % 16 == 0 ? "\n" : " ");
        }
        if (got % 16 != 0) std::fprintf(stderr, "\n");
    }
    pipe.close();
    return session.state() == iap2::AuthState::Failed ? 4 : 0;
}

static int run_bluetooth_capture(const std::string& executable,
                                 const std::string& address) {
    const char* user_home = std::getenv("HOME");
    const std::string bridge = executable.empty()
        ? std::string(user_home ? user_home : "") +
              "/Desktop/Projects/playport/macos/bt-bridge/bt-bridge"
        : executable;
    transport::BtBridgeTransport transport(bridge, address);
    if (!transport.open()) {
        std::fprintf(stderr, "capture: could not start Bluetooth bridge\n");
        return 3;
    }

    mfi::MockSigner signer;
    log::StderrLogger log;
    iap2::AuthSession session(signer, log);
    iap2::LinkHandshake handshake(transport, log);
    std::fprintf(stderr, "capture: sending iAP2 link handshake to %s\n", address.c_str());
    if (!handshake.sendSynOnly()) {
        std::fprintf(stderr, "capture: failed to send link marker/SYN\n");
        transport.close();
        return 3;
    }

    std::fprintf(stderr, "capture: logging received bytes in hex; Ctrl-C to stop\n");
    std::uint8_t buffer[4096];
    for (;;) {
        const int count = transport.read(buffer, sizeof buffer, 5000);
        if (count < 0) {
            std::fprintf(stderr, "capture: Bluetooth bridge closed\n");
            break;
        }
        if (count == 0) continue;
        for (int i = 0; i < count; ++i)
            std::fprintf(stderr, "%02x%s", buffer[i], (i + 1) % 16 == 0 ? "\n" : " ");
        if (count % 16 != 0) std::fprintf(stderr, "\n");
    }
    transport.close();
    return session.state() == iap2::AuthState::Failed ? 4 : 0;
}

int main(int argc, char** argv) {
    transport::UsbConfig cfg;
    bool simulate = false;
    bool pipe_mode = false;
    bool capture_mode = false;
    bool wireless_mode = false;
    bool receiver_mode = false;
    bool auth_only = false;
    std::string bind_addr = "0.0.0.0";
    std::string phone_address, bt_bridge_path;
    std::string mfi_cert_path, mfi_key_path;
    std::uint16_t port = 7000;
    bool list = false;
    std::string wifi_ip;
    iap2::IdentificationConfig ident_cfg;
    iap2::WirelessControlConfig wifi_cfg;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "%s needs a value\n", name); std::exit(2); }
            return argv[++i];
        };
        if (a == "--auth-only") auth_only = true;
        else if (a == "--simulate") simulate = true;
        else if (a == "--pipe") pipe_mode = true;
        else if (a == "--capture") capture_mode = true;
        else if (a == "--wireless") wireless_mode = true;
        else if (a == "--receiver") receiver_mode = true;
        else if (a == "--bind") bind_addr = next("--bind");
        else if (a == "--port") port = static_cast<std::uint16_t>(std::atoi(next("--port")));
        else if (a == "--list") list = true;
        else if (a == "--address") phone_address = next("--address");
        else if (a == "--bt-bridge") bt_bridge_path = next("--bt-bridge");
        else if (a == "--mfi-cert") mfi_cert_path = next("--mfi-cert");
        else if (a == "--mfi-key") mfi_key_path = next("--mfi-key");
        else if (a == "--bt-mac") ident_cfg.bluetooth_mac = next("--bt-mac");
        else if (a == "--wifi-ip") wifi_ip = next("--wifi-ip");
        else if (a == "--ssid") {
            ident_cfg.ssid = next("--ssid");
            wifi_cfg.ssid = ident_cfg.ssid;
        }
        else if (a == "--wifi-channel") wifi_cfg.channel = static_cast<std::uint8_t>(std::atoi(next("--wifi-channel")));
        else if (a == "--wifi-security") wifi_cfg.security_type = static_cast<std::uint8_t>(std::atoi(next("--wifi-security")));
        else if (a == "--vid") cfg.vid = static_cast<std::uint16_t>(std::strtoul(next("--vid"), nullptr, 0));
        else if (a == "--pid") cfg.pid = static_cast<std::uint16_t>(std::strtoul(next("--pid"), nullptr, 0));
        else if (a == "--interface") cfg.interface_number = std::atoi(next("--interface"));
        else if (a == "--ep-in") cfg.ep_in = static_cast<unsigned char>(std::strtoul(next("--ep-in"), nullptr, 0));
        else if (a == "--ep-out") cfg.ep_out = static_cast<unsigned char>(std::strtoul(next("--ep-out"), nullptr, 0));
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); usage(); return 2; }
    }

    if (auth_only && receiver_mode) { std::fprintf(stderr, "--auth-only cannot be combined with --receiver\n"); return 2; }
    if (list) {
        transport::list_usb_devices();
        return 0;
    }
    if (wireless_mode) {
        auto pairing = std::make_shared<airplay::PairingProtocol>();
        const std::string resolved_device_id = pairing->deviceIdentifier();
        ::setenv("CARPLAY_DEVICE_ID", resolved_device_id.c_str(), 1);
        if (ident_cfg.bluetooth_mac != "00:00:00:00:00:00")
            ::setenv("CARPLAY_BT_MAC", ident_cfg.bluetooth_mac.c_str(), 1);
        const pid_t advertisement = start_advertisement(argv[0], port);
        const int result = run_wireless(bind_addr, port, pairing);
        stop_advertisement(advertisement);
        return result;
    }
    if (capture_mode) {
        if (phone_address.empty()) {
            std::fprintf(stderr, "--capture requires --address PHONE\n");
            return 2;
        }
        return run_bluetooth_capture(bt_bridge_path, phone_address);
    }
    if (pipe_mode) return run_pipe();
    if (simulate) return run_simulate();
    if (receiver_mode && phone_address.empty()) {
        std::fprintf(stderr, "--receiver requires --address PHONE for the Bluetooth iAP2 link\n");
        return 2;
    }
    if (receiver_mode && wifi_ip.empty()) {
        std::fprintf(stderr, "--receiver requires --wifi-ip with the Mac hotspot's reachable address\n");
        return 2;
    }
    const char* wifi_passphrase = std::getenv("CARPLAY_WIFI_PASSPHRASE");
    if (receiver_mode && wifi_cfg.security_type != 0 &&
        wifi_cfg.passphrase.empty() && (!wifi_passphrase || !*wifi_passphrase)) {
        std::fprintf(stderr, "--receiver needs CARPLAY_WIFI_PASSPHRASE for a secured hotspot\n");
        return 2;
    }

    if (mfi_cert_path.empty() || mfi_key_path.empty()) {
        std::fprintf(stderr,
                     "live iAP2 mode requires --mfi-cert and --mfi-key; "
                     "use --pipe for raw capture without credentials\n");
        return 2;
    }
    std::string signer_error;
    auto loaded_signer = mfi::OpenSslSigner::load(mfi_cert_path, mfi_key_path, signer_error);
    if (!loaded_signer) {
        std::fprintf(stderr, "failed to load MFi signer: %s\n", signer_error.c_str());
        return 2;
    }
    std::shared_ptr<mfi::OpenSslSigner> signer(std::move(loaded_signer));
    if (receiver_mode) {
        auto pairing = std::make_shared<airplay::PairingProtocol>();
        const std::string resolved_device_id = pairing->deviceIdentifier();
        wifi_cfg.wireless_ip = wifi_ip;
        wifi_cfg.airplay_port = port;
        wifi_cfg.device_identifier = resolved_device_id;
        wifi_cfg.public_key = pairing->publicKeyHex();
        ::setenv("CARPLAY_DEVICE_ID", resolved_device_id.c_str(), 1);
        if (std::getenv("CARPLAY_BT_MAC") == nullptr &&
            ident_cfg.bluetooth_mac != "00:00:00:00:00:00")
            ::setenv("CARPLAY_BT_MAC", ident_cfg.bluetooth_mac.c_str(), 1);
        const pid_t advertisement = start_advertisement(argv[0], port);
        std::fprintf(stderr, "receiver: sharing Wi-Fi network '%s'; set up the hotspot before launch\n",
                     wifi_cfg.ssid.c_str());
        const int result = airplay::runVideoDisplayApplication(
            [=] { (void)run_live_iap2(cfg, phone_address, bt_bridge_path,
                    bind_addr, port, signer, pairing, ident_cfg, wifi_cfg, true); });
        stop_advertisement(advertisement);
        return result;
    }
    return run_live_iap2(cfg, phone_address, bt_bridge_path, bind_addr, port,
                         std::move(signer), {}, std::move(ident_cfg),
                         std::move(wifi_cfg), false, auth_only);
}
