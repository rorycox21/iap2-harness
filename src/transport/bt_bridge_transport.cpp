#include "transport/bt_bridge_transport.hpp"
#include <cerrno>
#include <csignal>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace transport {
BtBridgeTransport::BtBridgeTransport(std::string executable, std::string address)
    : executable_(std::move(executable)), address_(std::move(address)) {}

BtBridgeTransport::~BtBridgeTransport() { close(); }

bool BtBridgeTransport::open() {
    if (child_pid_ > 0) return true;
    int input_pipe[2], output_pipe[2];
    if (::pipe(input_pipe) != 0) return false;
    if (::pipe(output_pipe) != 0) {
        ::close(input_pipe[0]); ::close(input_pipe[1]);
        return false;
    }
    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(input_pipe[0]); ::close(input_pipe[1]);
        ::close(output_pipe[0]); ::close(output_pipe[1]);
        return false;
    }
    if (pid == 0) {
        ::dup2(input_pipe[0], STDIN_FILENO);
        ::dup2(output_pipe[1], STDOUT_FILENO);
        ::close(input_pipe[0]); ::close(input_pipe[1]);
        ::close(output_pipe[0]); ::close(output_pipe[1]);
        ::execl(executable_.c_str(), executable_.c_str(), "--address",
                address_.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    ::close(input_pipe[0]);
    ::close(output_pipe[1]);
    to_child_ = input_pipe[1];
    from_child_ = output_pipe[0];
    child_pid_ = static_cast<int>(pid);
    std::signal(SIGPIPE, SIG_IGN);
    return true;
}

int BtBridgeTransport::write(const std::uint8_t* data, std::size_t n) {
    if (to_child_ < 0) return -1;
    std::size_t sent = 0;
    while (sent < n) {
        const ssize_t count = ::write(to_child_, data + sent, n - sent);
        if (count < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (count == 0) return -1;
        sent += static_cast<std::size_t>(count);
    }
    return static_cast<int>(sent);
}

int BtBridgeTransport::read(std::uint8_t* data, std::size_t n, int timeout_ms) {
    if (from_child_ < 0) return -1;
    pollfd descriptor{from_child_, POLLIN, 0};
    const int ready = ::poll(&descriptor, 1, timeout_ms);
    if (ready == 0) return 0;
    if (ready < 0) return errno == EINTR ? 0 : -1;
    if (descriptor.revents & POLLNVAL) return -1;
    if (descriptor.revents & POLLIN) {
        const ssize_t count = ::read(from_child_, data, n);
        if (count < 0) return errno == EINTR ? 0 : -1;
        if (count == 0) return -1;
        return static_cast<int>(count);
    }
    if (descriptor.revents & (POLLERR | POLLHUP)) return -1;
    return 0;
}

void BtBridgeTransport::close() {
    if (to_child_ >= 0) { ::close(to_child_); to_child_ = -1; }
    if (from_child_ >= 0) { ::close(from_child_); from_child_ = -1; }
    if (child_pid_ > 0) {
        ::kill(child_pid_, SIGTERM);
        int status = 0;
        while (::waitpid(child_pid_, &status, 0) < 0 && errno == EINTR) {}
        child_pid_ = -1;
    }
}
}  // namespace transport
