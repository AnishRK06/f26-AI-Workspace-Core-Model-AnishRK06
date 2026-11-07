#include "aiws/ipc_protocol.hpp"

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>

namespace aiws::ipc {
namespace {

// ms left until the deadline, capped at one poll slice. 0 = time is up
int slice_until(Clock::time_point deadline) {
    const auto left =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    if (left <= 0) return 0;
    return static_cast<int>(std::min<long long>(left, kPollSliceMs));
}

// waits in short slices so a stop request or the deadline is noticed quickly,
// instead of one long poll() that could outlast both
bool wait_ready(int fd, short events, Clock::time_point deadline, const std::atomic<bool>* stop) {
    for (;;) {
        if (stop && stop->load()) return false;
        const int slice = slice_until(deadline);
        if (slice == 0) return false;

        pollfd p{fd, events, 0};
        const int ready = ::poll(&p, 1, slice);
        // ready can also mean HUP/ERR. the recv/send after this reports which
        if (ready > 0) return true;
        if (ready < 0 && errno != EINTR) return false;
        // 0 = this slice ran out, EINTR = interrupted. either way go again
    }
}

// recv/send failures that just mean "try again"
bool retryable(int error) { return error == EINTR || error == EAGAIN || error == EWOULDBLOCK; }

}  // namespace

std::string default_socket_path() {
    const char* user = std::getenv("USER");
    if (user && *user) return "/tmp/aiws-" + std::string(user) + ".sock";
    return "/tmp/aiws-" + std::to_string(::getuid()) + ".sock";
}

std::string encode(std::uint8_t code, const std::string& body) {
    const auto n = static_cast<std::uint32_t>(body.size());
    std::string out;
    out.reserve(kHeaderBytes + body.size());
    out.push_back(static_cast<char>(code));
    // big-endian: most significant byte first
    out.push_back(static_cast<char>((n >> 24) & 0xFF));
    out.push_back(static_cast<char>((n >> 16) & 0xFF));
    out.push_back(static_cast<char>((n >> 8) & 0xFF));
    out.push_back(static_cast<char>(n & 0xFF));
    out += body;
    return out;
}

std::uint32_t decode_length(const unsigned char* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24) | (static_cast<std::uint32_t>(bytes[1]) << 16) |
           (static_cast<std::uint32_t>(bytes[2]) << 8) | static_cast<std::uint32_t>(bytes[3]);
}

// one recv can return fewer bytes than asked for (the sender split its
// writes, or they just arrived that way), so keep going until we have all n
bool read_exact(int fd, void* buffer, std::size_t n, Clock::time_point deadline,
                const std::atomic<bool>* stop) {
    auto* out = static_cast<unsigned char*>(buffer);
    std::size_t got = 0;
    while (got < n) {
        if (!wait_ready(fd, POLLIN, deadline, stop)) return false;
        const ssize_t r = ::recv(fd, out + got, n - got, 0);
        if (r > 0) {
            got += static_cast<std::size_t>(r);
        } else if (r == 0) {
            return false;  // other side closed before sending everything
        } else if (!retryable(errno)) {
            return false;
        }
    }
    return true;
}

// MSG_NOSIGNAL: if the other side already hung up we get an error back here,
// instead of SIGPIPE killing the whole process
bool write_all(int fd, const void* buffer, std::size_t n, Clock::time_point deadline,
               const std::atomic<bool>* stop) {
    const auto* in = static_cast<const unsigned char*>(buffer);
    std::size_t sent = 0;
    while (sent < n) {
        if (!wait_ready(fd, POLLOUT, deadline, stop)) return false;
        const ssize_t w = ::send(fd, in + sent, n - sent, MSG_NOSIGNAL);
        if (w > 0) {
            sent += static_cast<std::size_t>(w);
        } else if (w < 0 && !retryable(errno)) {
            return false;
        }
    }
    return true;
}

}  // namespace aiws::ipc
