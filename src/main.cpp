#include "aiws/gui/main_window.hpp"

#include <QApplication>
#include <QMessageBox>

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

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    aiws::gui::MainWindow window;
    window.set_exit_callback(&confirm_exit);
    window.show();

    // hands control to Qt's event loop. from here on everything happens
    // because an event (click, edit, selection) fired a signal
    return app.exec();
}
