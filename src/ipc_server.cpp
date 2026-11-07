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

// the check behind exit code 0 vs 1. done here on the worker so the reply
// never waits for the GUI thread (CR-1 constraint 3). empty string = readable
std::string why_unreadable(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return std::strerror(errno);

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

    listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ < 0) throw std::runtime_error(std::string("socket: ") + std::strerror(errno));

    int bound = ::bind(listen_fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    if (bound < 0 && errno == EADDRINUSE && !another_app_listening(addr)) {
        ::unlink(socket_path_.c_str());
        bound = ::bind(listen_fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    }
    if (bound < 0 || ::listen(listen_fd_, 16) < 0) {
        const std::string reason = std::strerror(errno);
        ::close(listen_fd_);
        listen_fd_ = -1;
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

    stop_ = true;
    if (worker_.joinable()) worker_.join();

    // the worker is gone, so nothing else can be using the socket now
    if (listen_fd_ >= 0) ::close(listen_fd_);
    listen_fd_ = -1;
    ::unlink(socket_path_.c_str());
}

const std::string& IpcServer::socket_path() const noexcept { return socket_path_; }

// one client at a time is plenty for a script adding files. poll() in short
// slices instead of a blocking accept(), so stop_ is seen within 100 ms (E4)
void IpcServer::run() {
    while (!stop_) {
        pollfd p{listen_fd_, POLLIN, 0};
        const int ready = ::poll(&p, 1, ipc::kPollSliceMs);
        if (ready <= 0) continue;  // timeout or EINTR: check stop_ again

        const int client = ::accept(listen_fd_, nullptr, nullptr);
        if (client < 0) continue;  // client gave up before we got to it
        serve_client(client);
    }
}

void IpcServer::serve_client(int client_fd) {
    const ClientFd client(client_fd);
    set_non_blocking(client.get());
    const auto deadline = ipc::Clock::now() + std::chrono::milliseconds(ipc::kRequestTimeoutMs);

    // every read/write also gives up as soon as stop_ is set
    const auto reply = [&](std::uint8_t status, const std::string& reason) {
        const std::string bytes = ipc::encode(status, reason);
        ipc::write_all(client.get(), bytes.data(), bytes.size(), deadline, &stop_);
    };

    // header first: tells us how many path bytes are still coming (E3)
    unsigned char header[ipc::kHeaderBytes];
    if (!ipc::read_exact(client.get(), header, sizeof(header), deadline, &stop_)) return;

    const std::uint32_t size = ipc::decode_length(header + 1);
    if (header[0] != ipc::kCommandAdd) return reply(ipc::kStatusRejected, "unknown command");
    if (size == 0 || size > ipc::kMaxPathBytes) return reply(ipc::kStatusRejected, "bad path length");

    std::string path(size, '\0');
    if (!ipc::read_exact(client.get(), &path[0], size, deadline, &stop_)) return;

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
