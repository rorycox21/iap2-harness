// Encrypted CarPlay screen stream receiver based on the local DiPlay
// implementation. The framing and cryptographic fields remain UNVERIFIED
// until exercised against a live iPhone.
#include "airplay/screen_stream.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

namespace airplay {
namespace ac = fxchain::airplay;

namespace {
constexpr std::size_t kHeaderSize = 128;
constexpr std::uint32_t kMaxBody = 8 * 1024 * 1024;
constexpr std::uint8_t kVideoFrame = 0;
constexpr std::uint8_t kVideoConfig = 1;

bool readFully(int fd, std::uint8_t* dst, std::size_t size) {
    std::size_t read = 0;
    while (read < size) {
        const auto n = ::recv(fd, dst + read, size - read, 0);
        if (n == 0) return false;
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        read += static_cast<std::size_t>(n);
    }
    return true;
}

std::uint32_t readLe32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
           (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

std::uint32_t readBe32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}

std::uint64_t readLe64(const std::uint8_t* p) {
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

std::uint64_t monotonicNanos() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

ac::Bytes toAnnexB(const ac::Bytes& payload) {
    if (payload.size() >= 4 && payload[0] == 0 && payload[1] == 0 &&
        (payload[2] == 1 || (payload[2] == 0 && payload[3] == 1)))
        return payload;
    ac::Bytes out;
    std::size_t offset = 0;
    while (offset + 4 <= payload.size()) {
        const auto n = readBe32(payload.data() + offset);
        offset += 4;
        if (n == 0 || n > payload.size() - offset) return {};
        out.insert(out.end(), {0, 0, 0, 1});
        out.insert(out.end(), payload.begin() + offset, payload.begin() + offset + n);
        offset += n;
    }
    if (offset != payload.size()) return {};
    return out;
}

std::pair<std::string, ac::Bytes> detectConfig(const ac::Bytes& body) {
    for (std::size_t i = 4; i + 4 <= body.size(); ++i) {
        const std::string atom(reinterpret_cast<const char*>(body.data() + i), 4);
        if (atom == "avcC" || atom == "hvcC")
            return {atom == "avcC" ? "H264" : "H265",
                    ac::Bytes(body.begin() + i + 4, body.end())};
    }
    if (body.size() >= 9 && (body[5] & 0x1f) >= 1 &&
        std::size_t(((body[6] << 8) | body[7]) + 8) <= body.size() &&
        (body[8] & 0x1f) == 7)
        return {"H264", body};
    return {"H265", body};
}
} // namespace

ScreenStreamReceiver::ScreenStreamReceiver(ac::Bytes key, std::string expected_peer,
                                           ConfigHandler on_config,
                                           FrameHandler on_frame)
    : key_(std::move(key)), expected_peer_(std::move(expected_peer)),
      on_config_(std::move(on_config)),
      on_frame_(std::move(on_frame)) {}

ScreenStreamReceiver::~ScreenStreamReceiver() { close(); }

std::uint16_t ScreenStreamReceiver::start() {
    if (listen_fd_.load() >= 0 || key_.size() != 32) return 0;
    const int fd = ::socket(AF_INET6, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    listen_fd_.store(fd);
    int one = 1;
    int dual_stack = 0;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    ::setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &dual_stack, sizeof dual_stack);
    sockaddr_in6 addr{};
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_any;
    addr.sin6_port = 0;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0 ||
        ::listen(fd, 1) < 0) {
        close();
        return 0;
    }
    socklen_t len = sizeof addr;
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) < 0) {
        close();
        return 0;
    }
    port_ = ntohs(addr.sin6_port);
    worker_ = std::thread(&ScreenStreamReceiver::acceptLoop, this);
    return port_;
}

void ScreenStreamReceiver::close() {
    closing_.store(true);
    const int client = client_fd_.exchange(-1);
    if (client >= 0) { ::shutdown(client, SHUT_RDWR); ::close(client); }
    const int listener = listen_fd_.exchange(-1);
    if (listener >= 0) { ::shutdown(listener, SHUT_RDWR); ::close(listener); }
    if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) worker_.join();
}

void ScreenStreamReceiver::acceptLoop() {
    while (!closing_.load()) {
        const int listener = listen_fd_.load();
        if (listener < 0) break;
        sockaddr_storage peer{};
        socklen_t peer_len = sizeof peer;
        const int client = ::accept(listener, reinterpret_cast<sockaddr*>(&peer), &peer_len);
        if (client < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (!expected_peer_.empty()) {
            char peer_ip[INET6_ADDRSTRLEN]{};
            const void* peer_bytes = nullptr;
            if (peer.ss_family == AF_INET) {
                peer_bytes = &reinterpret_cast<sockaddr_in*>(&peer)->sin_addr;
            } else if (peer.ss_family == AF_INET6) {
                const auto* addr = &reinterpret_cast<sockaddr_in6*>(&peer)->sin6_addr;
                if (IN6_IS_ADDR_V4MAPPED(addr))
                    peer_bytes = addr->s6_addr + 12;
                else
                    peer_bytes = addr;
            }
            const int family = peer.ss_family == AF_INET ||
                (peer.ss_family == AF_INET6 &&
                 IN6_IS_ADDR_V4MAPPED(&reinterpret_cast<sockaddr_in6*>(&peer)->sin6_addr))
                    ? AF_INET : AF_INET6;
            if (!peer_bytes || !::inet_ntop(family, peer_bytes, peer_ip, sizeof peer_ip) ||
                expected_peer_ != peer_ip) {
                ::close(client);
                continue;
            }
        }
        client_fd_.store(client);
        receiveLoop(client);
        int expected = client;
        if (client_fd_.compare_exchange_strong(expected, -1)) ::close(client);
    }
}

void ScreenStreamReceiver::receiveLoop(int client) {
    std::array<std::uint8_t, kHeaderSize> header{};
    while (!closing_.load() && readFully(client, header.data(), header.size())) {
        const std::uint32_t body_size = readLe32(header.data());
        if (body_size > kMaxBody) break;
        ac::Bytes body(body_size);
        if (!readFully(client, body.data(), body.size())) break;
        if (!onMessage(ac::Bytes(header.begin(), header.end()), body)) break;
    }
}

bool ScreenStreamReceiver::onMessage(const ac::Bytes& header, const ac::Bytes& body) {
    if (header.size() != kHeaderSize) return false;
    if (header[4] == kVideoConfig) {
        auto config = detectConfig(body);
        if (on_config_) on_config_(config.first, config.second);
        return true;
    }
    if (header[4] != kVideoFrame || body.size() < 16) return header[4] != kVideoFrame;
    const std::uint64_t arrival = monotonicNanos();
    const auto plain = ac::chacha20Poly1305Decrypt(
        key_, ac::counterNonce8(frame_counter_), body,
        ac::Bytes(header.begin(), header.end()));
    ++frame_counter_;
    if (!plain) return false;
    auto annex_b = toAnnexB(*plain);
    if (annex_b.empty()) return false;
    const std::uint64_t q32 = readLe64(header.data() + 8);
    const std::uint64_t sender = (q32 >> 32) * 1000000000ULL +
        (((q32 & 0xffffffffULL) * 1000000000ULL) >> 32);
    if (on_frame_) on_frame_(annex_b, sender, arrival);
    return true;
}

} // namespace airplay
