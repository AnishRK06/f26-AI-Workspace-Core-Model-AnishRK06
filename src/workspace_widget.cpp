#include "aiws/gui/workspace_widget.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>

namespace aiws::gui {
namespace {

// startup/reset corpus text. one constant so reset() can't drift from startup
const char* const kCorpusNotBuilt = "Not built";

// bold section heading. every panel title uses this so they all look the same
QLabel* make_heading(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}

}  // namespace

// ownership: every child is created with new and given a parent (or added to
// a layout, which reparents it), so Qt deletes them when this widget dies.
// nothing here is ever deleted by hand
WorkspaceWidget::WorkspaceWidget(QWidget* parent) : QWidget(parent) {
    // left panel: Workspace
    auto* workspace_frame = new QFrame(this);
    workspace_frame->setFrameShape(QFrame::StyledPanel);

    add_document_button_ = new QPushButton("Add Document", workspace_frame);
    build_corpus_button_ = new QPushButton("Build Corpus", workspace_frame);
    document_list_ = new QListWidget(workspace_frame);
    document_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    corpus_status_label_ = make_heading(kCorpusNotBuilt, workspace_frame);

    auto* workspace_header = new QHBoxLayout;
    workspace_header->addWidget(make_heading("Workspace", workspace_frame));
    workspace_header->addWidget(add_document_button_);
    workspace_header->addWidget(build_corpus_button_);
    workspace_header->addStretch();

    auto* corpus_row = new QHBoxLayout;
    corpus_row->addWidget(new QLabel("Corpus:", workspace_frame));
    corpus_row->addWidget(corpus_status_label_);
    corpus_row->addStretch();

    // stretch factor 1 on the list: extra height goes to the list, not the
    // button row or the status line
    auto* workspace_layout = new QVBoxLayout(workspace_frame);
    workspace_layout->addLayout(workspace_header);
    workspace_layout->addWidget(document_list_, 1);
    workspace_layout->addLayout(corpus_row);

    // right panel: Selected Document
    auto* document_frame = new QFrame(this);
    document_frame->setFrameShape(QFrame::StyledPanel);

    document_title_label_ = new QLabel("-", document_frame);
    document_text_ = new QPlainTextEdit(document_frame);
    document_text_->setReadOnly(true);
    document_text_->setPlaceholderText("Select a document to view its contents.");

    auto* document_header = new QHBoxLayout;
    document_header->addWidget(make_heading("Selected Document:", document_frame));
    document_header->addWidget(document_title_label_, 1);

    // QPlainTextEdit scrolls on its own, so a long document stays inside the
    // panel instead of forcing the whole window to grow
    auto* document_layout = new QVBoxLayout(document_frame);
    document_layout->addLayout(document_header);
    document_layout->addWidget(document_text_, 1);

    // the two panels side by side, equal width
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(workspace_frame, 1);
    layout->addWidget(document_frame, 1);

    // local events
    // signal -> signal: a raw button click is re-emitted as this component's
    // own, meaningful signal. MainWindow only ever sees "add requested", never
    // which button it was. clicked(bool) can drive a no-arg signal because Qt
    // lets the receiver ignore trailing arguments
    connect(add_document_button_, &QPushButton::clicked,
            this, &WorkspaceWidget::add_document_requested);
    connect(build_corpus_button_, &QPushButton::clicked,
            this, &WorkspaceWidget::build_corpus_requested);

    // currentRowChanged(int) already uses -1 for "nothing selected", which is
    // exactly what document_selected(int) promises, so it forwards as-is
    connect(document_list_, &QListWidget::currentRowChanged,
            this, &WorkspaceWidget::document_selected);

    // startup state: empty workspace, so nothing to build yet
    set_build_corpus_enabled(false);
}

// display only: the Document itself lives in MainWindow's workspace_. the list
// row index and the workspace vector index line up because both only append
void WorkspaceWidget::add_document(const QString& title) { document_list_->addItem(title); }

// changing the current row makes the list emit currentRowChanged, which goes
// out as document_selected -> MainWindow shows the text. no backend work here
void WorkspaceWidget::select_document(int row) { document_list_->setCurrentRow(row); }

void WorkspaceWidget::show_selected_document(const QString& title, const QString& text) {
    document_title_label_->setText(title);
    document_text_->setPlainText(text);
}

void WorkspaceWidget::clear_selected_document() {
    document_title_label_->setText("-");
    document_text_->clear();
}

void WorkspaceWidget::set_corpus_status(const QString& text) { corpus_status_label_->setText(text); }

void WorkspaceWidget::set_build_corpus_enabled(bool enabled) { build_corpus_button_->setEnabled(enabled); }

// Resettable::reset override. MainWindow calls this through a Resettable*, so
// virtual dispatch lands here without MainWindow knowing the concrete type
//
// clearing the list fires currentRowChanged(-1), so MainWindow also clears its
// side of the selection through the normal signal path
void WorkspaceWidget::reset() {
    document_list_->clear();
    clear_selected_document();
    set_corpus_status(kCorpusNotBuilt);
    set_build_corpus_enabled(false);
}

}  // namespace aiws::gui
