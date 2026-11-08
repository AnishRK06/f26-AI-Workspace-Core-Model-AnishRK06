#include "aiws/gui/main_window.hpp"

#include "aiws/gui/context_widget.hpp"
#include "aiws/gui/resettable.hpp"
#include "aiws/gui/search_widget.hpp"
#include "aiws/gui/term_inspector_widget.hpp"
#include "aiws/gui/workspace_widget.hpp"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <exception>
#include <utility>

namespace aiws::gui {
namespace {

// startup size only. no setFixedSize anywhere, so the window stays resizable
constexpr int kInitialWidth = 900;
constexpr int kInitialHeight = 900;

// bordered panel around one component so each area reads as its own box
QFrame* wrap_in_panel(QWidget* content, QWidget* parent) {
    auto* frame = new QFrame(parent);
    frame->setFrameShape(QFrame::StyledPanel);
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(content);
    return frame;
}

}  // namespace

// MainWindow is the coordinator: it owns the backend (workspace_ and
// processing_core_ by value), builds the four components, and is the only
// place backend calls happen. components just display and emit signals
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("AI Workspace");
    resize(kInitialWidth, kInitialHeight);

    auto* central = new QWidget(this);
    setCentralWidget(central);

    // header (non-white background #1). the #appHeader selector keeps the
    // color on the frame only, so the labels inside don't each draw a box
    auto* header = new QFrame(central);
    header->setObjectName("appHeader");
    header->setStyleSheet("#appHeader { background-color: #6b1e2e; border-radius: 4px; }"
                          "#appHeader QLabel { color: white; background: transparent; }");

    auto* title = new QLabel("AI Workspace", header);
    QFont title_font = title->font();
    title_font.setPointSize(title_font.pointSize() + 8);
    title_font.setBold(true);
    title->setFont(title_font);

    auto* subtitle = new QLabel("Document Search and Context Explorer", header);
    QFont subtitle_font = subtitle->font();
    subtitle_font.setItalic(true);
    subtitle->setFont(subtitle_font);

    auto* header_layout = new QVBoxLayout(header);
    header_layout->setContentsMargins(14, 8, 14, 8);
    header_layout->setSpacing(0);
    header_layout->addWidget(title);
    header_layout->addWidget(subtitle);

    // components
    workspace_widget_ = new WorkspaceWidget(central);
    search_widget_ = new SearchWidget(central);
    term_inspector_widget_ = new TermInspectorWidget(central);
    context_widget_ = new ContextWidget(central);

    auto* bottom_row = new QHBoxLayout;
    bottom_row->addWidget(wrap_in_panel(term_inspector_widget_, central), 1);
    bottom_row->addWidget(wrap_in_panel(context_widget_, central), 1);

    // status / Reset / Exit row (non-white backgrounds #2 and #3)
    auto* status_caption = new QLabel("Current status:", central);
    QFont bold = status_caption->font();
    bold.setBold(true);
    status_caption->setFont(bold);
    status_label_ = new QLabel(central);

    reset_button_ = new QPushButton("Reset", central);
    reset_button_->setStyleSheet(
        "QPushButton { background-color: #d9a441; color: #2b1d00; font-weight: bold;"
        " border: 1px solid #b5862c; border-radius: 4px; padding: 5px 28px; }"
        "QPushButton:pressed { background-color: #c08f30; }");

    exit_button_ = new QPushButton("Exit", central);
    exit_button_->setStyleSheet(
        "QPushButton { background-color: #2f6db5; color: white; font-weight: bold;"
        " border: 1px solid #245a96; border-radius: 4px; padding: 5px 28px; }"
        "QPushButton:pressed { background-color: #245a96; }");

    auto* status_row = new QHBoxLayout;
    status_row->addWidget(status_caption);
    status_row->addWidget(status_label_, 1);
    status_row->addWidget(reset_button_);
    status_row->addWidget(exit_button_);

    // overall vertical layout. the stretch factors decide how extra height is
    // shared on resize: search gets the most since the table needs it
    auto* main_layout = new QVBoxLayout(central);
    main_layout->addWidget(header);
    main_layout->addWidget(workspace_widget_, 3);
    main_layout->addWidget(wrap_in_panel(search_widget_, central), 4);
    main_layout->addLayout(bottom_row, 2);
    main_layout->addLayout(status_row);

    // component signal -> MainWindow slot. every user request ends up here,
    // and these slots are the only code that talks to the backend
    connect(workspace_widget_, &WorkspaceWidget::add_document_requested, this, &MainWindow::add_document);
    connect(workspace_widget_, &WorkspaceWidget::build_corpus_requested, this, &MainWindow::build_corpus);
    connect(workspace_widget_, &WorkspaceWidget::document_selected, this, &MainWindow::document_selected);

    connect(search_widget_, &SearchWidget::search_requested, this, &MainWindow::search_requested);
    connect(search_widget_, &SearchWidget::query_changed, this, &MainWindow::query_changed);
    connect(search_widget_, &SearchWidget::source_document_requested,
            this, &MainWindow::source_document_requested);

    // component -> component: the selected chunk id goes straight to the
    // Term Inspector. no backend involved, it just remembers it for TF
    connect(search_widget_, &SearchWidget::selected_chunk_changed,
            term_inspector_widget_, &TermInspectorWidget::set_selected_chunk);

    connect(term_inspector_widget_, &TermInspectorWidget::term_inspection_requested,
            this, &MainWindow::term_inspection_requested);
    connect(context_widget_, &ContextWidget::build_context_requested,
            this, &MainWindow::build_context_requested);

    connect(reset_button_, &QPushButton::clicked, this, &MainWindow::reset_application);
    connect(exit_button_, &QPushButton::clicked, this, &MainWindow::exit_application);

    status_label_->setText("Ready");
    update_action_states();
}

void MainWindow::set_exit_callback(ExitCallback callback) noexcept { exit_callback_ = callback; }

void MainWindow::set_build_delay_ms(int ms_per_document) { build_delay_ms_ = ms_per_document; }

void MainWindow::set_core_factory(CorpusBuildJob::CoreFactory factory) noexcept { core_factory_ = factory; }

bool MainWindow::is_corpus_current() const noexcept { return corpus_current_; }

bool MainWindow::is_building() const noexcept { return build_job_ != nullptr; }

std::size_t MainWindow::document_count() const noexcept { return workspace_.documents().size(); }

// the dialog is the only part that's specific to the button. everything else
// is add_document_file, so the button, the tests and CR-1 all add the same way
void MainWindow::add_document() {
    const QString path = QFileDialog::getOpenFileName(this, "Add Document", QString(),
                                                      "Text files (*.txt);;All files (*)");
    if (path.isEmpty()) return;  // cancelled: change nothing

    // add_document_file changes nothing on an unreadable file, not even the
    // status line, so the button reports that one itself
    if (!add_document_file(path) && !QFileInfo(path).isReadable()) {
        status_label_->setText("Could not open " + path);
    }
}

// the Workspace changes here, so this is more than a list update: the old
// corpus no longer matches and has to be invalidated
bool MainWindow::add_document_file(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    const QString text = QString::fromUtf8(file.readAll());

    // id = absolute path, title = file name, per the spec
    const QFileInfo info(path);
    const QString title = info.fileName();

    try {
        workspace_.add_document(
            Document(info.absoluteFilePath().toStdString(), title.toStdString(), text.toStdString()));
    } catch (const std::exception& e) {
        status_label_->setText(QString("Could not add %1: %2").arg(title, e.what()));
        return false;
    }

    // R2: a build that's running right now was started for the old workspace.
    // bumping the generation is what makes build_finished() throw it away
    ++build_generation_;

    workspace_widget_->add_document(title);
    invalidate_corpus();
    status_label_->setText("Added " + title + ". Build Corpus to process the workspace.");
    return true;
}

// always an explicit user request, adding a document never rebuilds on its own.
// M4: the build runs on a CorpusBuildJob's thread, so this returns right away
// and the GUI stays usable (R1). the job gets a copy of the workspace, never
// workspace_ itself. one build at a time: Build Corpus is disabled until
// build_finished() has handled the running one
void MainWindow::build_corpus() {
    if (workspace_.documents().empty() || build_job_) return;

    // if the corpus is current it stays usable while this runs: the workspace
    // hasn't changed, so the old corpus still matches it
    build_job_ = std::make_unique<CorpusBuildJob>(workspace_, build_delay_ms_, build_generation_, this,
                                                  "build_finished", core_factory_);

    workspace_widget_->set_corpus_status("Building...");
    status_label_->setText("Building corpus...");
    update_action_states();
}

// runs on the GUI thread, queued by the job once its result is in the future.
// the one place that decides whether a finished build becomes current (R2)
void MainWindow::build_finished() {
    if (!build_job_) return;

    // valid only if nothing changed the workspace since the build started.
    // a generation number, not a doc count: Reset + re-adding the same files
    // gives the same count but is still a different workspace
#ifdef AIWS_FAULT_ACCEPT_STALE
    const bool still_current = true;  // fault: accepts a build from an old workspace
#else
    const bool still_current = build_job_->generation() == build_generation_;
#endif

    ProcessingCore built;
    bool failed = false;
    QString reason;
    try {
        built = build_job_->take_result();
    } catch (const std::exception& e) {
        failed = true;
        reason = e.what();
    }

    // joins the worker. it already filled the promise and posted this call,
    // so all that's left of it is returning: the join doesn't block the GUI
    build_job_.reset();

    if (!still_current) {
        // built (or failed) for a workspace that no longer exists. not shown
        invalidate_corpus();
        status_label_->setText("Workspace changed during the build, so it was discarded. Build Corpus again.");
        return;
    }

    if (failed) {
        // R3: reported, and the app stays usable (Build is enabled again)
        corpus_current_ = false;
        clear_processing_outputs();
        workspace_widget_->set_corpus_status("Build failed");
        status_label_->setText(QString("Build Corpus failed: %1").arg(reason));
        update_action_states();
        return;
    }

    // the old core is destroyed here, on the GUI thread
    processing_core_ = std::move(built);
    corpus_current_ = true;
    clear_processing_outputs();
    const auto chunks = static_cast<qulonglong>(processing_core_.chunk_count());
    workspace_widget_->set_corpus_status(QString("%1 chunks").arg(chunks));
    status_label_->setText(QString("Corpus built: %1 chunks.").arg(chunks));
    update_action_states();
}

// presentation only. selecting a document never builds, searches or inspects
void MainWindow::document_selected(int row) {
    const auto& docs = workspace_.documents();
    if (row < 0 || static_cast<std::size_t>(row) >= docs.size()) {
        workspace_widget_->clear_selected_document();
        return;
    }
    const Document& d = docs[static_cast<std::size_t>(row)];
    workspace_widget_->show_selected_document(QString::fromStdString(d.title()),
                                              QString::fromStdString(d.text()));
}

// zero results is a valid search. set_results clears the old selection and
// details first, and the old context goes too since it came from a previous query
void MainWindow::search_requested(const QString& query, int k) {
    if (!corpus_current_ || query.trimmed().isEmpty()) return;

    std::vector<SearchResult> results;
    try {
        results = processing_core_.search(query.toStdString(), k);
    } catch (const std::exception& e) {
        status_label_->setText(QString("Search failed: %1").arg(e.what()));
        return;
    }

    const auto count = static_cast<qulonglong>(results.size());
    search_widget_->set_results(std::move(results));
    context_widget_->clear_context();
    status_label_->setText(QString("Search returned %1 result(s).").arg(count));
    update_action_states();
}

// editing the query only changes which buttons are enabled. it never searches
// and never clears the results already on screen
void MainWindow::query_changed(const QString& query) {
    (void)query;  // update_action_states reads it back through has_query()
    update_action_states();
}

// uses the current query and k from the search box, not the rows in the
// table. build_context runs its own search internally
void MainWindow::build_context_requested(int token_budget) {
    if (!corpus_current_ || !search_widget_->has_query()) return;

    std::vector<ContextItem> items;
    try {
        items = processing_core_.build_context(search_widget_->query().toStdString(),
                                               search_widget_->result_count(),
                                               static_cast<std::size_t>(token_budget));
    } catch (const std::exception& e) {
        status_label_->setText(QString("Build Context failed: %1").arg(e.what()));
        return;
    }

    context_widget_->set_context(items);
    status_label_->setText(QString("Context built: %1 item(s), budget %2 tokens.")
                               .arg(static_cast<qulonglong>(items.size()))
                               .arg(token_budget));
}

// list rows and workspace_ indices line up (both only append), so the
// matching index is the row to select. no new search happens here
void MainWindow::source_document_requested(const QString& document_id) {
    const std::string id = document_id.toStdString();
    const auto& docs = workspace_.documents();

    for (std::size_t i = 0; i < docs.size(); ++i) {
        if (docs[i].id() != id) continue;

        // selecting the row emits document_selected, which shows the text.
        // if that row was already selected no signal fires, so show it directly too
        workspace_widget_->select_document(static_cast<int>(i));
        document_selected(static_cast<int>(i));
        status_label_->setText("Showing source: " + QString::fromStdString(docs[i].title()));
        return;
    }
    status_label_->setText("Source document not found in the workspace.");
}

// DF never needs a selection. TF only when a chunk id came along. the backend
// throws if the term normalizes to more than one token, which ends up in the
// status line instead of crashing
void MainWindow::term_inspection_requested(const QString& term, const QString& chunk_id) {
    if (!corpus_current_ || term.trimmed().isEmpty()) return;

    try {
        const std::string t = term.toStdString();
        const std::size_t df = processing_core_.document_frequency(t);
        const bool has_chunk = !chunk_id.isEmpty();
        const std::size_t tf = has_chunk ? processing_core_.term_frequency(t, chunk_id.toStdString()) : 0;
        term_inspector_widget_->set_statistics(df, has_chunk, tf);
        status_label_->setText("Inspected term \"" + term + "\".");
    } catch (const std::exception& e) {
        term_inspector_widget_->clear_statistics();
        status_label_->setText(QString("Inspect failed: %1").arg(e.what()));
    }
}

// virtual dispatch: all four components are treated as plain Resettables, and
// each reset() call lands in that component's own override at runtime.
// MainWindow never needs to know which concrete widget it's resetting
void MainWindow::reset_application() {
    Resettable* const components[] = {workspace_widget_, search_widget_, term_inspector_widget_,
                                      context_widget_};
    for (Resettable* c : components) c->reset();

    // a running build was started for the workspace we're about to clear.
    // it keeps running on its own copy, and build_finished() throws it away
    ++build_generation_;

    // a fresh core drops the old corpus entirely. ProcessingCore is move-only,
    // so this is a move assignment from a temporary
    workspace_.clear();
    processing_core_ = ProcessingCore{};
    corpus_current_ = false;

    status_label_->setText("Application reset.");
    update_action_states();
}

// the decision (confirm, quit, or stay) lives in the callback, not here.
// null check because nothing guarantees a callback was ever registered
void MainWindow::exit_application() {
    if (exit_callback_) exit_callback_(this);
}

// CR-1. the server binds right away, so a bad path is reported here instead of
// failing later on its thread. it lives as long as the window: ~MainWindow
// destroys it, which stops it and removes the socket file (E4)
bool MainWindow::start_remote_add(const std::string& socket_path) {
    try {
        ipc_server_ = std::make_unique<IpcServer>(socket_path, this, "remote_add_requested");
    } catch (const std::exception& e) {
        status_label_->setText(QString("Remote add unavailable: %1").arg(e.what()));
        return false;
    }
    return true;
}

// queued from the server thread, so this runs on the GUI thread like any
// other click. E1: same function as Add Document, so a remote add during a
// build bumps the generation and invalidates exactly the same way (R2)
void MainWindow::remote_add_requested(const QString& path) {
    if (!add_document_file(path)) status_label_->setText("Remote add failed: could not open " + path);
}

// the one place that decides availability. called after every state change
// so the buttons can never disagree with corpus_current_ or the workspace
void MainWindow::update_action_states() {
    const bool has_documents = !workspace_.documents().empty();
    const bool has_query = search_widget_->has_query();

    // one build at a time (R4: builds can't pile up threads)
    workspace_widget_->set_build_corpus_enabled(has_documents && !build_job_);
    search_widget_->set_search_enabled(corpus_current_ && has_query);
    term_inspector_widget_->set_inspection_enabled(corpus_current_);
    context_widget_->set_build_context_enabled(corpus_current_ && has_query);
}

// the central invariant: search, inspect and context may only use a corpus
// that is current for the present workspace. anything built from the old
// one comes off the screen here
void MainWindow::invalidate_corpus() {
    corpus_current_ = false;
    clear_processing_outputs();
    workspace_widget_->set_corpus_status(processing_core_.chunk_count() == 0
                                             ? "Not built"
                                             : "Out of date (rebuild required)");
    update_action_states();
}

// clear_results also tells the Term Inspector "no chunk selected"
void MainWindow::clear_processing_outputs() {
    search_widget_->clear_results();
    term_inspector_widget_->clear_statistics();
    context_widget_->clear_context();
}

}  // namespace aiws::gui
