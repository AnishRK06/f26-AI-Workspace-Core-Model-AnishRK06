#pragma once

#include <string>
#include <thread>

class QObject;

namespace aiws::gui {

// CR-1 Remote Add server. Listens on a Unix domain socket on its own
// std::thread, answers each aiws_send request itself, and hands accepted
// paths to the GUI thread with a queued call to the receiver's slot. The
// worker never touches MainWindow, its Workspace, or any widget.
//
// threads: the constructor, stop() and the destructor run on the GUI thread.
// run() and serve_client() are the only worker-thread code
class IpcServer final {
public:
    // binds and starts listening right here on the GUI thread, so a bad
    // socket path fails right away (std::runtime_error) instead of silently
    // on the worker. then starts the worker. receiver has to outlive the
    // server (MainWindow owns it, so it does). add_slot takes one QString
    IpcServer(std::string socket_path, QObject* receiver, const char* add_slot);

    // stop(). never leaves a joinable thread or a socket file behind
    ~IpcServer();

    // owns a running thread and a socket, so copying or moving makes no sense
    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;
    IpcServer(IpcServer&&) = delete;
    IpcServer& operator=(IpcServer&&) = delete;

    // E4: wakes the worker through the wake pipe and joins it (it returns
    // right away, there's no slice to wait out), then closes the sockets and
    // removes the socket file. safe to call more than once
    void stop() noexcept;

    const std::string& socket_path() const noexcept;

private:
    // worker thread: wait for a client, serve it, repeat until woken to stop
    void run();

    // worker thread: read one request, reply, and post the path if accepted
    void serve_client(int client_fd);

    // closes both ends of the wake pipe. GUI thread, only once the worker is
    // joined (or before it ever started)
    void close_wake_pipe() noexcept;

    // set once in the constructor and only read after that
    std::string socket_path_;
    QObject* receiver_;
    const char* add_slot_;
    int listen_fd_{-1};

    // M5: how stop() wakes the worker. the worker's poll() watches the read
    // end next to the socket, with no timeout, so it sleeps until a client or
    // stop() shows up instead of waking every 100 ms to check a flag. both
    // ends are opened before the thread starts and closed after it's joined,
    // and the kernel does the synchronizing for the one byte that goes through
    int wake_read_fd_{-1};
    int wake_write_fd_{-1};

    // GUI thread only. makes stop() safe to call twice
    bool stopped_{false};

    // declared last on purpose: everything above exists before run() starts
    std::thread worker_;
};

}  // namespace aiws::gui
