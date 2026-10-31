#pragma once
#include "transport/transport.hpp"
#include <functional>
#include <cstdint>
#include <string>

namespace transport {

// TCP transport: implements Transport over a POSIX TCP socket.
// Portable to Linux, macOS, and QNX (all provide BSD sockets).
//
// Two modes:
//   - connect(): client mode, connects to host:port
//   - listen():  server mode, binds and accepts one connection
//
// For the wireless CarPlay receiver, use listen() — the iPhone is the
// TCP client, the receiver is the server.
class TcpTransport : public Transport {
public:
    TcpTransport();
    ~TcpTransport() override;

    // Client mode: connect to host:port. Returns false on error.
    bool connect(const std::string& host, std::uint16_t port, int timeout_ms = 5000);

    // Server mode: bind to bind_addr:port and accept one connection.
    // Returns false on bind/listen/accept error.
    bool listen(const std::string& bind_addr, std::uint16_t port, int timeout_ms = 30000, std::function<void()> on_listening = {});

    // Transport interface.
    bool open() override;   // no-op for TcpTransport; connect/listen already opened
    int  write(const std::uint8_t* data, std::size_t n) override;
    int  read(std::uint8_t* data, std::size_t n, int timeout_ms) override;
    void close() override;

    // The local address (useful for logging "listening on ...").
    std::string localAddress() const;

    // The accepted peer address in server mode; empty for other states.
    std::string peerAddress() const override;

private:
    int fd_ = -1;
    std::string local_addr_;
    std::string peer_addr_;
};

}  // namespace transport
