#include "aiws/ipc_protocol.hpp"

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <limits>

namespace aiws::ipc {
namespace {

// ms left until the deadline. 0 = time is up
int ms_until(Clock::time_point deadline) {
    const auto left =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    if (left <= 0) return 0;
    return static_cast<int>(std::min<long long>(left, std::numeric_limits<int>::max()));
}

// one poll() for the whole time left, no slices: it returns when fd is ready,
// when wake_fd says stop, or at the deadline, whichever comes first. so the
// thread sleeps the whole time instead of waking up to check a flag
bool wait_ready(int fd, short events, Clock::time_point deadline, int wake_fd) {
    for (;;) {
        const int timeout = ms_until(deadline);
        if (timeout == 0) return false;

        // poll() skips an entry whose fd is negative, so -1 just means no wake pipe
        std::array<pollfd, 2> p{{{fd, events, 0}, {wake_fd, POLLIN, 0}}};
        const int ready = ::poll(p.data(), p.size(), timeout);
        if (ready < 0 && errno != EINTR) return false;
        if (ready <= 0) continue;  // EINTR, or the deadline: the check above decides

        // asked to stop. checked first, so a stop always wins over more data
        if (p[1].revents != 0) return false;
        // ready can also mean HUP/ERR. the recv/send after this reports which
        return true;
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
bool read_exact(int fd, void* buffer, std::size_t n, Clock::time_point deadline, int wake_fd) {
    auto* out = static_cast<unsigned char*>(buffer);
    std::size_t got = 0;
    while (got < n) {
        if (!wait_ready(fd, POLLIN, deadline, wake_fd)) return false;
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
bool write_all(int fd, const void* buffer, std::size_t n, Clock::time_point deadline, int wake_fd) {
    const auto* in = static_cast<const unsigned char*>(buffer);
    std::size_t sent = 0;
    while (sent < n) {
        if (!wait_ready(fd, POLLOUT, deadline, wake_fd)) return false;
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
