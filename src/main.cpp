#include "aiws/gui/main_window.hpp"
#include "aiws/ipc_protocol.hpp"

#include <QApplication>
#include <QMessageBox>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

// matches MainWindow::ExitCallback, void (*)(QWidget*). MainWindow calls this
// through a plain function pointer, so it has to be a free function (a member
// function or a capturing lambda wouldn't convert to that pointer type)
void confirm_exit(QWidget* parent) {
    const auto answer = QMessageBox::question(parent, "Exit AI Workspace", "Are you sure you want to exit?",
                                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes) QApplication::quit();
    // No: just return. the app and its state are untouched
}

// command-line options. only main() uses these, so they live here
struct Options {
    int build_delay_ms = 0;                                    // --build-delay-ms N
    std::string ipc_socket = aiws::ipc::default_socket_path();  // --ipc-socket PATH
};

// whole string has to be a non-negative number, so "10ms" or "-5" are errors
// instead of quietly turning into something else
bool parse_delay(const std::string& text, int& out) {
    if (text.empty()) return false;
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (*end != '\0' || value < 0 || value > 600000) return false;
    out = static_cast<int>(value);
    return true;
}

// QApplication already took out the arguments that are Qt's own (like
// -platform offscreen), so only ours are left in argv here
bool parse_options(int argc, char* argv[], Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool has_value = i + 1 < argc;

        if (arg == "--build-delay-ms") {
            if (!has_value || !parse_delay(argv[++i], options.build_delay_ms)) {
                std::cerr << "ai_workspace_m4: --build-delay-ms needs a whole number of ms (0 or more)\n";
                return false;
            }
        } else if (arg == "--ipc-socket") {
            if (!has_value) {
                std::cerr << "ai_workspace_m4: --ipc-socket needs a PATH\n";
                return false;
            }
            options.ipc_socket = argv[++i];
        } else {
            // not fatal: the GUI is still perfectly usable without it
            std::cerr << "ai_workspace_m4: ignoring unknown option " << arg << '\n';
        }
    }
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    Options options;
    if (!parse_options(argc, argv, options)) {
        std::cerr << "usage: ai_workspace_m4 [--build-delay-ms N] [--ipc-socket PATH]\n";
        return 1;
    }

    aiws::gui::MainWindow window;
    window.set_exit_callback(&confirm_exit);
    window.set_build_delay_ms(options.build_delay_ms);

    // CR-1. if the socket can't be set up the app still runs, just without
    // remote add. the reason is on stderr and in the status line
    if (!window.start_remote_add(options.ipc_socket)) {
        std::cerr << "ai_workspace_m4: remote add is unavailable on " << options.ipc_socket << '\n';
    }

    window.show();

    // hands control to Qt's event loop. from here on everything happens
    // because an event (click, edit, selection, a finished build, a remote
    // add) fired a signal or a queued call. when exec() returns, window goes
    // out of scope: ~MainWindow joins the build and server threads and
    // removes the socket file (R4, E4)
    return app.exec();
}
