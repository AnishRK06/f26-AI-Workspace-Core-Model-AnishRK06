// aiws_send: CR-1 client. sends one document path to a running AI Workspace
// over its Unix domain socket and reports what the app said.
//
//   aiws_send [--socket PATH] add FILE
//
// exit codes (CR-1):
//   0  the app received the request and the file is readable
//   1  the app rejected it (reason on stderr). also used for bad usage,
//      since nothing was sent and 2 means "couldn't reach the app"
//   2  no app reachable within 2 s, or the exchange failed within 5 s
//
// the byte format lives in aiws/ipc_protocol.hpp so this and the app's
// server can never disagree on it

#include "aiws/ipc_protocol.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

namespace {

constexpr int kExitAccepted = 0;
constexpr int kExitRejected = 1;
constexpr int kExitUnreachable = 2;

// closes the socket on every return path, so no early return can leak it
class SocketFd {
public:
    explicit SocketFd(int fd) : fd_(fd) {}
    ~SocketFd() {
        if (fd_ >= 0) ::close(fd_);
    }
    SocketFd(const SocketFd&) = delete;
    SocketFd& operator=(const SocketFd&) = delete;

    int get() const { return fd_; }

private:
    int fd_;
};

int usage(const char* reason) {
    std::cerr << "aiws_send: " << reason << '\n'
              << "usage: aiws_send [--socket PATH] add FILE\n";
    return kExitRejected;
}

// E1: a relative FILE means relative to *our* working directory. the app runs
// somewhere else, so it has to get an absolute path. no realpath() here: if
// the file doesn't exist the app should be the one to say so (exit 1)
bool make_absolute(const std::string& file, std::string& out) {
    if (!file.empty() && file.front() == '/') {
        out = file;
        return true;
    }
    std::string cwd(4096, '\0');
    if (!::getcwd(&cwd[0], cwd.size())) return false;
    cwd.resize(std::strlen(cwd.c_str()));
    out = cwd + "/" + file;
    return true;
}

// non-blocking connect with a 2 s limit. a Unix socket connect either works
// right away or fails right away (no file = ENOENT, nobody listening =
// ECONNREFUSED), except EAGAIN when the app's accept backlog is full. only
// that case is worth retrying until the deadline
bool connect_to(int fd, const std::string& socket_path) {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (socket_path.size() >= sizeof(addr.sun_path)) return false;  // won't fit
    std::memcpy(addr.sun_path, socket_path.c_str(), socket_path.size() + 1);

    const auto deadline = aiws::ipc::Clock::now() + std::chrono::milliseconds(aiws::ipc::kConnectTimeoutMs);
    for (;;) {
        if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0) return true;
        if (errno != EAGAIN && errno != EINTR) return false;
        if (aiws::ipc::Clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string socket_path = aiws::ipc::default_socket_path();
    int i = 1;
    if (i < argc && std::string(argv[i]) == "--socket") {
        if (i + 1 >= argc) return usage("--socket needs a PATH");
        socket_path = argv[i + 1];
        i += 2;
    }
    if (argc - i != 2 || std::string(argv[i]) != "add") return usage("expected: add FILE");

    const std::string file = argv[i + 1];
    if (file.empty()) return usage("FILE must not be empty");

    std::string path;
    if (!make_absolute(file, path)) {
        std::cerr << "aiws_send: could not read the current directory\n";
        return kExitRejected;
    }
    if (path.size() > aiws::ipc::kMaxPathBytes) return usage("FILE path is too long");

    SocketFd sock(::socket(AF_UNIX, SOCK_STREAM, 0));
    if (sock.get() < 0) {
        std::cerr << "aiws_send: could not create a socket: " << std::strerror(errno) << '\n';
        return kExitUnreachable;
    }
    // non-blocking so nothing below can hang past its deadline. read_exact and
    // write_all poll() before every recv/send, so they're fine with it
    ::fcntl(sock.get(), F_SETFL, ::fcntl(sock.get(), F_GETFL) | O_NONBLOCK);

    if (!connect_to(sock.get(), socket_path)) {
        std::cerr << "aiws_send: no AI Workspace is listening on " << socket_path << '\n';
        return kExitUnreachable;
    }

    // one 5 s budget for the whole exchange: send the request, get the reply
    const auto deadline = aiws::ipc::Clock::now() + std::chrono::milliseconds(aiws::ipc::kExchangeTimeoutMs);

    const std::string request = aiws::ipc::encode(aiws::ipc::kCommandAdd, path);
    if (!aiws::ipc::write_all(sock.get(), request.data(), request.size(), deadline)) {
        std::cerr << "aiws_send: sending the request failed\n";
        return kExitUnreachable;
    }

    unsigned char header[aiws::ipc::kHeaderBytes];
    if (!aiws::ipc::read_exact(sock.get(), header, sizeof(header), deadline)) {
        std::cerr << "aiws_send: no reply from the AI Workspace\n";
        return kExitUnreachable;
    }
    const std::uint8_t status = header[0];
    const std::uint32_t reason_size = aiws::ipc::decode_length(header + 1);

    // a reply we don't understand counts as a failed exchange, not a rejection
    if ((status != aiws::ipc::kStatusAccepted && status != aiws::ipc::kStatusRejected) ||
        reason_size > aiws::ipc::kMaxPathBytes) {
        std::cerr << "aiws_send: malformed reply from the AI Workspace\n";
        return kExitUnreachable;
    }

    std::string reason(reason_size, '\0');
    if (reason_size > 0 && !aiws::ipc::read_exact(sock.get(), &reason[0], reason_size, deadline)) {
        std::cerr << "aiws_send: reply was cut off\n";
        return kExitUnreachable;
    }

    if (status == aiws::ipc::kStatusAccepted) return kExitAccepted;
    std::cerr << "aiws_send: rejected: " << reason << '\n';
    return kExitRejected;
}
