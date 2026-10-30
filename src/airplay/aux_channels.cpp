// AirPlay NTP timing responder and encrypted event TCP endpoint. Framing
// follows local DiPlay source; live compatibility remains UNVERIFIED.
#include "airplay/aux_channels.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace airplay {
namespace ac = fxchain::airplay;
namespace {
constexpr std::size_t kMaxChunk = 16384;

std::uint16_t readLe16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (std::uint16_t(p[1]) << 8));
}
void writeLe16(std::uint8_t* p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
}
void writeNtp(std::uint8_t* p) {
    using namespace std::chrono;
    const auto elapsed = system_clock::now().time_since_epoch();
    const auto seconds = duration_cast<std::chrono::seconds>(elapsed);
    const auto nanos = duration_cast<std::chrono::nanoseconds>(elapsed - seconds);
    const std::uint64_t ntp_sec = static_cast<std::uint64_t>(seconds.count()) + 2208988800ULL;
    const std::uint64_t frac = (static_cast<std::uint64_t>(nanos.count()) << 32) / 1000000000ULL;
    const std::uint64_t value = (ntp_sec << 32) | frac;
    for (int i = 0; i < 8; ++i) p[i] = static_cast<std::uint8_t>(value >> (56 - 8 * i));
}
bool sendAll(int fd, const std::uint8_t* data, std::size_t size) {
    std::size_t done = 0;
    while (done < size) {
        const auto n = ::send(fd, data + done, size - done, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        done += static_cast<std::size_t>(n);
    }
    return true;
}
bool nextRequest(std::string& buffer, std::string& cseq, bool& is_request) {
    const auto header_end = buffer.find("\r\n\r\n");
    if (header_end == std::string::npos) return false;
    const auto line_end = buffer.find("\r\n");
    const std::string request_line = buffer.substr(0, line_end);
    is_request = request_line.rfind("RTSP/", 0) != 0 &&
                 request_line.rfind("HTTP/", 0) != 0;
    std::size_t pos = line_end == std::string::npos ? buffer.size() : line_end + 2;
    std::size_t body_size = 0;
    cseq = "1";
    while (pos < header_end) {
        const auto end = buffer.find("\r\n", pos);
        const std::string line = buffer.substr(pos, end - pos);
        const auto colon = line.find(':');
        if (colon != std::string::npos) {
            std::string name = line.substr(0, colon);
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            std::string value = line.substr(colon + 1);
            while (!value.empty() && value.front() == ' ') value.erase(value.begin());
            if (name == "cseq") cseq = value;
            if (name == "content-length") {
                try { body_size = static_cast<std::size_t>(std::stoul(value)); }
                catch (...) { return false; }
            }
        }
        if (end == std::string::npos || end >= header_end) break;
        pos = end + 2;
    }
    const std::size_t total = header_end + 4 + body_size;
    if (buffer.size() < total) return false;
    buffer.erase(0, total);
    return true;
}
} // namespace

AirPlayAuxChannels::AirPlayAuxChannels(ac::Bytes shared_secret)
    : event_read_key_(ac::hkdfSha512("Events-Salt", "Events-Read-Encryption-Key", shared_secret)),
      event_write_key_(ac::hkdfSha512("Events-Salt", "Events-Write-Encryption-Key", shared_secret)) {}

AirPlayAuxChannels::~AirPlayAuxChannels() { close(); }

bool AirPlayAuxChannels::start() {
    if (timing_fd_.load() >= 0 || event_listen_fd_.load() >= 0 ||
        event_read_key_.size() != 32 || event_write_key_.size() != 32) return false;
    int off = 0, one = 1;
    int udp = ::socket(AF_INET6, SOCK_DGRAM, 0);
    if (udp < 0) return false;
    ::setsockopt(udp, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof off);
    ::setsockopt(udp, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in6 udp_addr{};
    udp_addr.sin6_family = AF_INET6;
    udp_addr.sin6_addr = in6addr_any;
    if (::bind(udp, reinterpret_cast<sockaddr*>(&udp_addr), sizeof udp_addr) < 0) {
        ::close(udp); return false;
    }
    socklen_t addr_len = sizeof udp_addr;
    if (::getsockname(udp, reinterpret_cast<sockaddr*>(&udp_addr), &addr_len) < 0) {
        ::close(udp); return false;
    }
    timing_fd_.store(udp);
    timing_port_ = ntohs(udp_addr.sin6_port);

    int tcp = ::socket(AF_INET6, SOCK_STREAM, 0);
    if (tcp < 0) { close(); return false; }
    ::setsockopt(tcp, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof off);
    ::setsockopt(tcp, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in6 tcp_addr{};
    tcp_addr.sin6_family = AF_INET6;
    tcp_addr.sin6_addr = in6addr_any;
    if (::bind(tcp, reinterpret_cast<sockaddr*>(&tcp_addr), sizeof tcp_addr) < 0 ||
        ::listen(tcp, 1) < 0) {
        ::close(tcp); close(); return false;
    }
    addr_len = sizeof tcp_addr;
    if (::getsockname(tcp, reinterpret_cast<sockaddr*>(&tcp_addr), &addr_len) < 0) {
        ::close(tcp); close(); return false;
    }
    event_listen_fd_.store(tcp);
    event_port_ = ntohs(tcp_addr.sin6_port);
    closing_.store(false);
    timing_thread_ = std::thread(&AirPlayAuxChannels::timingLoop, this);
    event_thread_ = std::thread(&AirPlayAuxChannels::eventAcceptLoop, this);
    return true;
}

bool AirPlayAuxChannels::sendEncryptedEvent(int client,
                                             const ac::Bytes& plaintext) {
    std::lock_guard<std::mutex> lock(event_write_mutex_);
    for (std::size_t offset = 0; offset < plaintext.size(); offset += kMaxChunk) {
        const auto count = std::min(kMaxChunk, plaintext.size() - offset);
        std::array<std::uint8_t, 2> header{};
        writeLe16(header.data(), static_cast<std::uint16_t>(count));
        const ac::Bytes chunk(plaintext.begin() + offset,
                              plaintext.begin() + offset + count);
        const auto sealed = ac::chacha20Poly1305Encrypt(
            event_write_key_, ac::counterNonce8(event_write_counter_++), chunk,
            ac::Bytes(header.begin(), header.end()));
        if (!sendAll(client, header.data(), header.size()) ||
            !sendAll(client, sealed.data(), sealed.size())) return false;
    }
    return true;
}

bool AirPlayAuxChannels::sendTouch(bool down, std::uint16_t x, std::uint16_t y) {
    const int client = event_client_fd_.load();
    if (client < 0) return false;

    // Match the reference's two-contact report. The Mac pointer uses slot 0;
    // slot 1 remains present and released.
    ac::Bytes report(12, 0);
    report[0] = 0;
    report[1] = down ? 1 : 0;
    report[2] = static_cast<std::uint8_t>(x);
    report[3] = static_cast<std::uint8_t>(x >> 8);
    report[4] = static_cast<std::uint8_t>(y);
    report[5] = static_cast<std::uint8_t>(y >> 8);
    report[6] = 1;

    const auto body = fxchain::airplay::bplist::encode(
        fxchain::airplay::bplist::Value::object({
            {"type", fxchain::airplay::bplist::Value::str("hidSendReport")},
            {"uuid", fxchain::airplay::bplist::Value::str("2a2a2a2a")},
            {"hidReport", fxchain::airplay::bplist::Value::bytes(report)}}));
    const std::string header = std::string(
        "POST /command RTSP/1.0\r\n"
        "Content-Type: application/x-apple-binary-plist\r\n") +
        "Content-Length: " + std::to_string(body.size()) + "\r\n" +
        "CSeq: " + std::to_string(++event_cseq_) + "\r\n\r\n";
    ac::Bytes request(header.begin(), header.end());
    request.insert(request.end(), body.begin(), body.end());
    return sendEncryptedEvent(client, request);
}

void AirPlayAuxChannels::close() {
    closing_.store(true);
    const int client = event_client_fd_.exchange(-1);
    if (client >= 0) { ::shutdown(client, SHUT_RDWR); ::close(client); }
    const int listener = event_listen_fd_.exchange(-1);
    if (listener >= 0) { ::shutdown(listener, SHUT_RDWR); ::close(listener); }
    const int udp = timing_fd_.exchange(-1);
    if (udp >= 0) { ::shutdown(udp, SHUT_RDWR); ::close(udp); }
    if (timing_thread_.joinable() && timing_thread_.get_id() != std::this_thread::get_id())
        timing_thread_.join();
    if (event_thread_.joinable() && event_thread_.get_id() != std::this_thread::get_id())
        event_thread_.join();
}

void AirPlayAuxChannels::timingLoop() {
    std::array<std::uint8_t, 2048> input{};
    while (!closing_.load()) {
        const int fd = timing_fd_.load();
        if (fd < 0) break;
        sockaddr_storage peer{};
        socklen_t peer_len = sizeof peer;
        const auto got = ::recvfrom(fd, input.data(), input.size(), 0,
                                    reinterpret_cast<sockaddr*>(&peer), &peer_len);
        if (got < 0) { if (errno == EINTR) continue; break; }
        if (got < 32 || input[1] != 210) continue;
        std::array<std::uint8_t, 32> response{};
        response[0] = 0x80;
        response[1] = 211;
        response[3] = 7;
        std::copy(input.begin() + 24, input.begin() + 32, response.begin() + 8);
        writeNtp(response.data() + 16);
        writeNtp(response.data() + 24);
        ::sendto(fd, response.data(), response.size(), 0,
                 reinterpret_cast<sockaddr*>(&peer), peer_len);
    }
}

void AirPlayAuxChannels::eventAcceptLoop() {
    const int listener = event_listen_fd_.load();
    if (listener < 0 || closing_.load()) return;
    const int client = ::accept(listener, nullptr, nullptr);
    if (client < 0) return;
    event_client_fd_.store(client);
    eventLoop(client);
    int expected = client;
    if (event_client_fd_.compare_exchange_strong(expected, -1)) ::close(client);
}

void AirPlayAuxChannels::eventLoop(int client) {
    std::vector<std::uint8_t> encrypted;
    std::string plaintext;
    std::array<std::uint8_t, 8192> input{};
    std::uint64_t read_counter = 0;
    while (!closing_.load()) {
        const auto n = ::recv(client, input.data(), input.size(), 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        encrypted.insert(encrypted.end(), input.begin(), input.begin() + n);
        while (encrypted.size() >= 2) {
            const auto length = readLe16(encrypted.data());
            if (length > kMaxChunk) return;
            const std::size_t packet_size = 2 + length + 16;
            if (encrypted.size() < packet_size) break;
            const ac::Bytes aad(encrypted.begin(), encrypted.begin() + 2);
            const ac::Bytes sealed(encrypted.begin() + 2, encrypted.begin() + packet_size);
            const auto opened = ac::chacha20Poly1305Decrypt(
                event_read_key_, ac::counterNonce8(read_counter++), sealed, aad);
            if (!opened) return;
            plaintext.append(reinterpret_cast<const char*>(opened->data()), opened->size());
            encrypted.erase(encrypted.begin(), encrypted.begin() + packet_size);
        }
        std::string cseq;
        bool is_request = false;
        while (nextRequest(plaintext, cseq, is_request)) {
            if (!is_request) continue;
            const std::string response = std::string("RTSP/1.0 200 OK\r\nCSeq: ") + cseq +
                                         "\r\nContent-Length: 0\r\n\r\n";
            const ac::Bytes bytes(response.begin(), response.end());
            if (!sendEncryptedEvent(client, bytes)) return;
        }
    }
}

} // namespace airplay
