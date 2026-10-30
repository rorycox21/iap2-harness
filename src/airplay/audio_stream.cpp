// AirPlay type-100 encrypted RTP receiver based on DiPlay's AudioStream
// bytecode. RTP encryption layout is source-derived and UNVERIFIED with a
// live iPhone; decoded audio playback is intentionally not implemented here.
#include "airplay/audio_stream.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace airplay {
namespace ac = fxchain::airplay;

namespace {
int bindUdp(std::uint16_t& port) {
    const int fd = ::socket(AF_INET6, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    int off = 0;
    int one = 1;
    timeval timeout{1, 0};
    ::setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof off);
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    sockaddr_in6 address{};
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_any;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof address) < 0) {
        ::close(fd);
        return -1;
    }
    socklen_t length = sizeof address;
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) < 0) {
        ::close(fd);
        return -1;
    }
    port = ntohs(address.sin6_port);
    return fd;
}
} // namespace

AudioStreamReceiver::AudioStreamReceiver(ac::Bytes key, std::string expected_peer,
                                         PacketHandler on_packet)
    : key_(std::move(key)), expected_peer_(std::move(expected_peer)),
      on_packet_(std::move(on_packet)) {}

AudioStreamReceiver::~AudioStreamReceiver() { close(); }

std::pair<std::uint16_t, std::uint16_t> AudioStreamReceiver::start() {
    if (key_.size() != 32 || data_fd_.load() >= 0 || closing_.load()) return {};
    std::uint16_t data_port = 0;
    std::uint16_t control_port = 0;
    const int data = bindUdp(data_port);
    if (data < 0) return {};
    const int control = bindUdp(control_port);
    if (control < 0) {
        ::close(data);
        return {};
    }
    data_fd_.store(data);
    control_fd_.store(control);
    data_thread_ = std::thread(&AudioStreamReceiver::dataLoop, this);
    control_thread_ = std::thread(&AudioStreamReceiver::controlLoop, this);
    return {data_port, control_port};
}

void AudioStreamReceiver::close() {
    if (closing_.exchange(true)) return;
    const int data = data_fd_.exchange(-1);
    const int control = control_fd_.exchange(-1);
    if (data >= 0) { ::shutdown(data, SHUT_RDWR); ::close(data); }
    if (control >= 0) { ::shutdown(control, SHUT_RDWR); ::close(control); }
    if (data_thread_.joinable() && data_thread_.get_id() != std::this_thread::get_id())
        data_thread_.join();
    if (control_thread_.joinable() && control_thread_.get_id() != std::this_thread::get_id())
        control_thread_.join();
}

bool AudioStreamReceiver::peerMatches(const sockaddr_storage& peer) const {
    if (expected_peer_.empty()) return true;
    char address[INET6_ADDRSTRLEN]{};
    const void* bytes = nullptr;
    int family = AF_INET6;
    if (peer.ss_family == AF_INET) {
        family = AF_INET;
        bytes = &reinterpret_cast<const sockaddr_in*>(&peer)->sin_addr;
    } else if (peer.ss_family == AF_INET6) {
        const auto* ip = &reinterpret_cast<const sockaddr_in6*>(&peer)->sin6_addr;
        if (IN6_IS_ADDR_V4MAPPED(ip)) {
            family = AF_INET;
            bytes = ip->s6_addr + 12;
        } else {
            bytes = ip;
        }
    }
    return bytes && ::inet_ntop(family, bytes, address, sizeof address) &&
           expected_peer_ == address;
}

void AudioStreamReceiver::dataLoop() {
    ac::Bytes packet(65536);
    while (!closing_.load()) {
        const int fd = data_fd_.load();
        if (fd < 0) return;
        sockaddr_storage peer{};
        socklen_t peer_length = sizeof peer;
        const auto size = ::recvfrom(fd, packet.data(), packet.size(), 0,
                                     reinterpret_cast<sockaddr*>(&peer), &peer_length);
        if (size < 0) {
            if (errno == EINTR) continue;
            if (closing_.load() || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return;
        }
        if (!peerMatches(peer) || size < 36) continue;

        const std::size_t wire_size = static_cast<std::size_t>(size);
        const ac::Bytes nonce(packet.begin() + wire_size - 8, packet.begin() + wire_size);
        const ac::Bytes aad(packet.begin(), packet.begin() + 4);
        const ac::Bytes sealed(packet.begin() + 12, packet.begin() + wire_size - 8);
        const auto plain = ac::chacha20Poly1305Decrypt(key_, nonce, sealed, aad);
        if (!plain) continue;

        ac::Bytes rtp(packet.begin(), packet.begin() + 12);
        rtp.insert(rtp.end(), plain->begin(), plain->end());
        if (on_packet_) on_packet_(rtp);
    }
}

void AudioStreamReceiver::controlLoop() {
    ac::Bytes packet(4096);
    while (!closing_.load()) {
        const int fd = control_fd_.load();
        if (fd < 0) return;
        sockaddr_storage peer{};
        socklen_t peer_length = sizeof peer;
        const auto size = ::recvfrom(fd, packet.data(), packet.size(), 0,
                                     reinterpret_cast<sockaddr*>(&peer), &peer_length);
        if (size < 0) {
            if (errno == EINTR) continue;
            if (closing_.load() || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return;
        }
    }
}

} // namespace airplay
