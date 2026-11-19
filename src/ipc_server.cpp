#include "aiws/gui/ipc_server.hpp"

#include "aiws/ipc_protocol.hpp"

#include <QMetaObject>
#include <QObject>
#include <QString>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace aiws::gui {
namespace {

// closes a client socket on every return path out of serve_client
class ClientFd {
public:
    explicit ClientFd(int fd) : fd_(fd) {}
    ~ClientFd() {
        if (fd_ >= 0) ::close(fd_);
    }
    ClientFd(const ClientFd&) = delete;
    ClientFd& operator=(const ClientFd&) = delete;

    int get() const { return fd_; }

private:
    int fd_;
};

void set_non_blocking(int fd) { ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK); }

sockaddr_un make_address(const std::string& path) {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) throw std::runtime_error("socket path too long: " + path);
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    return addr;
}

// a socket file left behind by a crashed run makes bind() fail with
// EADDRINUSE. if nobody answers on it, it's stale and safe to remove. if
// someone does answer, another AI Workspace is really running there
bool another_app_listening(const sockaddr_un& addr) {
    const int probe = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (probe < 0) return false;
    const bool answered = ::connect(probe, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0;
    ::close(probe);
    return answered;
}

// errno as text. std::strerror can hand back one buffer shared by every
// thread, and this file uses it on both the GUI thread and the worker, so
// it's strerror_r with a buffer of our own instead (GNU version: returns the text)
std::string error_text(int error) {
    std::array<char, 256> buffer{};
    return ::strerror_r(error, buffer.data(), buffer.size());
}

// the check behind exit code 0 vs 1. done here on the worker so the reply
// never waits for the GUI thread (CR-1 constraint 3). empty string = readable
std::string why_unreadable(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return error_text(errno);

    struct stat info {};
    const bool regular = ::fstat(fd, &info) == 0 && S_ISREG(info.st_mode);
    ::close(fd);
    // a directory opens fine with O_RDONLY, but Add Document can't read it
    return regular ? std::string() : "not a regular file";
}

}  // namespace

IpcServer::IpcServer(std::string socket_path, QObject* receiver, const char* add_slot)
    : socket_path_(std::move(socket_path)), receiver_(receiver), add_slot_(add_slot) {
    const sockaddr_un addr = make_address(socket_path_);

    // the wake pipe first, so if it fails there's no socket to clean up yet.
    // non-blocking so the one write in stop() can never block
    std::array<int, 2> wake{};
    if (::pipe2(wake.data(), O_CLOEXEC | O_NONBLOCK) < 0) {
        throw std::runtime_error("pipe: " + error_text(errno));
    }
    wake_read_fd_ = wake[0];
    wake_write_fd_ = wake[1];

    listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        const std::string reason = error_text(errno);
        close_wake_pipe();
        throw std::runtime_error("socket: " + reason);
    }

    int bound = ::bind(listen_fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    if (bound < 0 && errno == EADDRINUSE && !another_app_listening(addr)) {
        ::unlink(socket_path_.c_str());
        bound = ::bind(listen_fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    }
    if (bound < 0 || ::listen(listen_fd_, 16) < 0) {
        const std::string reason = error_text(errno);
        ::close(listen_fd_);
        listen_fd_ = -1;
        close_wake_pipe();
        throw std::runtime_error("cannot listen on " + socket_path_ + ": " + reason);
    }
    // non-blocking so accept() after poll() can never hang the worker
    set_non_blocking(listen_fd_);

    // last, so the thread only ever sees a fully set up server
    worker_ = std::thread(&IpcServer::run, this);
}

IpcServer::~IpcServer() { stop(); }

void IpcServer::stop() noexcept {
    if (stopped_) return;
    stopped_ = true;

    // one byte makes the read end readable for good (nobody reads it), so
    // every poll() the worker is in, or gets to later, returns right away
    const char byte = 1;
    if (wake_write_fd_ >= 0) (void)::write(wake_write_fd_, &byte, 1);
    if (worker_.joinable()) worker_.join();

    // the worker is gone, so nothing else can be using the sockets now
    if (listen_fd_ >= 0) ::close(listen_fd_);
    listen_fd_ = -1;
    close_wake_pipe();
    ::unlink(socket_path_.c_str());
}

void IpcServer::close_wake_pipe() noexcept {
    if (wake_read_fd_ >= 0) ::close(wake_read_fd_);
    if (wake_write_fd_ >= 0) ::close(wake_write_fd_);
    wake_read_fd_ = -1;
    wake_write_fd_ = -1;
}

const std::string& IpcServer::socket_path() const noexcept { return socket_path_; }

// one client at a time is plenty for a script adding files. poll() with no
// timeout: the thread sleeps until a client connects or stop() writes to the
// wake pipe, so it never wakes up just to check (M5 constraint 3, E4)
void IpcServer::run() {
    for (;;) {
        std::array<pollfd, 2> p{{{listen_fd_, POLLIN, 0}, {wake_read_fd_, POLLIN, 0}}};
        const int ready = ::poll(p.data(), p.size(), -1);
        if (ready < 0) continue;        // EINTR: wait again
        if (p[1].revents != 0) return;  // stop() woke us

        const int client = ::accept(listen_fd_, nullptr, nullptr);
        if (client < 0) continue;  // client gave up before we got to it
        serve_client(client);
    }
}

void IpcServer::serve_client(int client_fd) {
    const ClientFd client(client_fd);
    set_non_blocking(client.get());
    const auto deadline = ipc::Clock::now() + std::chrono::milliseconds(ipc::kRequestTimeoutMs);

    // every read/write also gives up as soon as stop() writes to the wake pipe
    const auto reply = [&](std::uint8_t status, const std::string& reason) {
        const std::string bytes = ipc::encode(status, reason);
        ipc::write_all(client.get(), bytes.data(), bytes.size(), deadline, wake_read_fd_);
    };

    // header first: tells us how many path bytes are still coming (E3)
    unsigned char header[ipc::kHeaderBytes];
    if (!ipc::read_exact(client.get(), header, sizeof(header), deadline, wake_read_fd_)) return;

    const std::uint32_t size = ipc::decode_length(header + 1);
    if (header[0] != ipc::kCommandAdd) return reply(ipc::kStatusRejected, "unknown command");
    if (size == 0 || size > ipc::kMaxPathBytes) return reply(ipc::kStatusRejected, "bad path length");

    std::string path(size, '\0');
    if (!ipc::read_exact(client.get(), &path[0], size, deadline, wake_read_fd_)) return;

    // aiws_send always sends an absolute path. a NUL inside would silently
    // cut the path short in open(), so it's rejected instead
    if (path.front() != '/') return reply(ipc::kStatusRejected, "path must be absolute");
    if (path.find('\0') != std::string::npos) return reply(ipc::kStatusRejected, "path contains a NUL byte");

    const std::string problem = why_unreadable(path);
    if (!problem.empty()) return reply(ipc::kStatusRejected, path + ": " + problem);

    // E1: hand it to the GUI thread first, then reply. so by the time
    // aiws_send exits 0, the add is already queued behind anything the user
    // did before it. QueuedConnection: this thread never waits for the GUI
    QMetaObject::invokeMethod(receiver_, add_slot_, Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(path)));
    reply(ipc::kStatusAccepted, std::string());
}

}  // namespace aiws::gui
