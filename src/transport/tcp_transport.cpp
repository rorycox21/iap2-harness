#include "transport/tcp_transport.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdio>

namespace transport {

static void set_reuse(int fd) {
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
}

TcpTransport::TcpTransport() = default;
TcpTransport::~TcpTransport() { close(); }

bool TcpTransport::connect(const std::string& host, std::uint16_t port, int timeout_ms) {
    close();
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) { std::perror("socket"); return false; }

    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &sa.sin_addr) != 1) {
        std::fprintf(stderr, "tcp: bad address %s\n", host.c_str());
        close();
        return false;
    }

    // Non-blocking connect with timeout.
    int flags = fcntl(fd_, F_GETFL, 0);
    fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
    int rc = ::connect(fd_, reinterpret_cast<sockaddr*>(&sa), sizeof sa);
    if (rc < 0 && errno != EINPROGRESS) {
        std::perror("connect");
        close();
        return false;
    }
    pollfd pfd{fd_, POLLOUT, 0};
    if (poll(&pfd, 1, timeout_ms) <= 0) {
        std::fprintf(stderr, "tcp: connect timeout\n");
        close();
        return false;
    }
    int err = 0; socklen_t elen = sizeof err;
    getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &elen);
    if (err != 0) {
        std::fprintf(stderr, "tcp: connect error %d\n", err);
        close();
        return false;
    }
    fcntl(fd_, F_SETFL, flags);

    local_addr_ = host + ":" + std::to_string(port);
    sockaddr_in peer{};
    socklen_t peer_len = sizeof peer;
    if (::getpeername(fd_, reinterpret_cast<sockaddr*>(&peer), &peer_len) == 0) {
        char peer_ip[INET_ADDRSTRLEN]{};
        if (::inet_ntop(AF_INET, &peer.sin_addr, peer_ip, sizeof peer_ip))
            peer_addr_ = peer_ip;
    }
    std::fprintf(stderr, "tcp: connected to %s\n", local_addr_.c_str());
    return true;
}

bool TcpTransport::listen(const std::string& bind_addr, std::uint16_t port, int timeout_ms,
                          std::function<void()> on_listening) {
    close();
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) { std::perror("socket"); return false; }
    set_reuse(fd_);

    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    if (bind_addr.empty() || bind_addr == "0.0.0.0") {
        sa.sin_addr.s_addr = INADDR_ANY;
    } else if (inet_pton(AF_INET, bind_addr.c_str(), &sa.sin_addr) != 1) {
        std::fprintf(stderr, "tcp: bad bind address %s\n", bind_addr.c_str());
        close();
        return false;
    }

    if (::bind(fd_, reinterpret_cast<sockaddr*>(&sa), sizeof sa) < 0) {
        std::perror("bind");
        close();
        return false;
    }
    if (::listen(fd_, 1) < 0) {
        std::perror("listen");
        close();
        return false;
    }

    local_addr_ = (bind_addr.empty() ? "0.0.0.0" : bind_addr) + ":" + std::to_string(port);
    std::fprintf(stderr, "tcp: listening on %s\n", local_addr_.c_str());

    // Readiness means the socket is bound and listening, not that a peer has connected.
    if (on_listening) on_listening();

    // Wait for one connection.
    pollfd pfd{fd_, POLLIN, 0};
    int prc = poll(&pfd, 1, timeout_ms);
    if (prc <= 0) {
        std::fprintf(stderr, "tcp: accept timeout\n");
        close();
        return false;
    }

    sockaddr_in peer{};
    socklen_t plen = sizeof peer;
    int client = ::accept(fd_, reinterpret_cast<sockaddr*>(&peer), &plen);
    if (client < 0) {
        std::perror("accept");
        close();
        return false;
    }
    // Close the listening socket; keep only the accepted one.
    ::close(fd_);
    fd_ = client;

    char peer_ip[64];
    if (!inet_ntop(AF_INET, &peer.sin_addr, peer_ip, sizeof peer_ip))
        peer_ip[0] = '\0';
    peer_addr_ = peer_ip;
    std::fprintf(stderr, "tcp: accepted connection from %s:%u\n",
                 peer_ip, ntohs(peer.sin_port));
    return true;
}

bool TcpTransport::open() { return fd_ >= 0; }

int TcpTransport::write(const std::uint8_t* data, std::size_t n) {
    if (fd_ < 0) return -1;
    std::size_t total = 0;
    while (total < n) {
        ssize_t w = ::send(fd_, data + total, n - total, 0);
        if (w <= 0) {
            if (errno == EINTR) continue;
            std::perror("send");
            return -1;
        }
        total += static_cast<std::size_t>(w);
    }
    return static_cast<int>(total);
}

int TcpTransport::read(std::uint8_t* data, std::size_t n, int timeout_ms) {
    if (fd_ < 0) return -1;
    pollfd pfd{fd_, POLLIN, 0};
    int prc = poll(&pfd, 1, timeout_ms);
    if (prc == 0) return 0;  // timeout
    if (prc < 0) { std::perror("poll"); return -1; }
    ssize_t r = ::recv(fd_, data, n, 0);
    if (r < 0) {
        if (errno == EINTR) return 0;
        std::perror("recv");
        return -1;
    }
    if (r == 0) return -1;  // peer closed
    return static_cast<int>(r);
}

void TcpTransport::close() {
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    peer_addr_.clear();
}

std::string TcpTransport::localAddress() const { return local_addr_; }
std::string TcpTransport::peerAddress() const { return peer_addr_; }

}  // namespace transport
