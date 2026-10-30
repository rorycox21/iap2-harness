#pragma once
#include "transport/transport.hpp"
#include <cstdio>

namespace transport {

// Transport over stdin/stdout. Used to connect iap2-harness to the
// bt-bridge process, which pumps bytes to the iPhone's iAP2 RFCOMM
// channel. PipeTransport requires a full-duplex shell connection; a one-way
// `bridge | harness` pipeline cannot send the SYN back to the bridge. In bash,
// use a coprocess:
//
//   coproc BT { ./bt-bridge --address <iphone>; }
//   ./iap2-harness --pipe <&"${BT[0]}" >&"${BT[1]}"
//
// For a direct launch, --capture --address PHONE uses the same full-duplex
// bridge transport internally.
class PipeTransport : public Transport {
public:
    bool open() override { return true; }
    int write(const std::uint8_t* data, std::size_t n) override {
        std::size_t w = std::fwrite(data, 1, n, stdout);
        std::fflush(stdout);
        return static_cast<int>(w);
    }
    int read(std::uint8_t* data, std::size_t n, int /*timeout_ms*/) override {
        std::size_t r = std::fread(data, 1, n, stdin);
        return static_cast<int>(r);
    }
    void close() override {}
};

}  // namespace transport
