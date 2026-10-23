#include "aiws/gui/term_inspector_widget.hpp"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace aiws::gui {
namespace {

// neutral display for "no statistic available" (em dash)
const char* const kNeutralValue = "—";

}  // namespace

TermInspectorWidget::TermInspectorWidget(QWidget* parent) : QWidget(parent) {
    auto* heading = new QLabel("Term Inspector:", this);
    QFont bold = heading->font();
    bold.setBold(true);
    heading->setFont(bold);

    term_edit_ = new QLineEdit(this);
    term_edit_->setPlaceholderText("Enter a term");
    inspect_button_ = new QPushButton("Inspect", this);

    document_frequency_value_ = new QLabel(kNeutralValue, this);
    chunk_frequency_value_ = new QLabel(kNeutralValue, this);

    auto* term_row = new QHBoxLayout;
    term_row->addWidget(new QLabel("Term:", this));
    term_row->addWidget(term_edit_, 1);
    term_row->addWidget(inspect_button_);

    // grid keeps each caption next to its value. stretch on the value columns
    // so the two pairs spread across the panel instead of bunching left
    auto* stats = new QGridLayout;
    stats->addWidget(new QLabel("Document Frequency:", this), 0, 0);
    stats->addWidget(document_frequency_value_, 0, 1);
    stats->addWidget(new QLabel("Selected Chunk TF:", this), 0, 2);
    stats->addWidget(chunk_frequency_value_, 0, 3);
    stats->setColumnStretch(1, 1);
    stats->setColumnStretch(3, 1);

    // trailing stretch pushes everything to the top when the panel is taller
    // than its contents
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(heading);
    layout->addLayout(term_row);
    layout->addLayout(stats);
    layout->addStretch();

    // local events. Enter in the term box is the same request as Inspect
    connect(inspect_button_, &QPushButton::clicked, this, &TermInspectorWidget::request_inspection);
    connect(term_edit_, &QLineEdit::returnPressed, this, &TermInspectorWidget::request_inspection);

    // startup state: no corpus, so nothing to inspect
    set_inspection_enabled(false);
}

void TermInspectorWidget::set_inspection_enabled(bool enabled) { inspect_button_->setEnabled(enabled); }

// only remembers which chunk TF would use. selecting a result must not
// inspect on its own, but the numbers on screen described the old chunk, so
// they're cleared until the user clicks Inspect again
void TermInspectorWidget::set_selected_chunk(const QString& chunk_id) {
    if (chunk_id == selected_chunk_id_) return;
    selected_chunk_id_ = chunk_id;
    clear_statistics();
}

// DF is always shown. TF only when a chunk was selected, otherwise it stays
// neutral instead of showing a misleading 0
void TermInspectorWidget::set_statistics(std::size_t document_frequency,
                                         bool has_chunk_frequency,
                                         std::size_t chunk_frequency) {
    document_frequency_value_->setText(QString::number(static_cast<qulonglong>(document_frequency)));
    chunk_frequency_value_->setText(has_chunk_frequency
                                        ? QString::number(static_cast<qulonglong>(chunk_frequency))
                                        : QString(kNeutralValue));
}

void TermInspectorWidget::clear_statistics() {
    document_frequency_value_->setText(kNeutralValue);
    chunk_frequency_value_->setText(kNeutralValue);
}

// Resettable::reset override, reached through virtual dispatch from MainWindow
void TermInspectorWidget::reset() {
    term_edit_->clear();
    selected_chunk_id_.clear();
    clear_statistics();
    set_inspection_enabled(false);
}

// empty or whitespace-only term: no request at all, per the spec. the button
// check stops Enter from inspecting a stale corpus
void TermInspectorWidget::request_inspection() {
    const QString term = term_edit_->text().trimmed();
    if (!inspect_button_->isEnabled() || term.isEmpty()) return;
    emit term_inspection_requested(term, selected_chunk_id_);
}

}  // namespace aiws::gui
