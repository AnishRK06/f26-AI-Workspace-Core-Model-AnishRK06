#include "aiws/gui/search_widget.hpp"

#include "aiws/gui/search_result_model.hpp"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QString>
#include <QTableView>
#include <QVBoxLayout>

#include <utility>

namespace aiws::gui {
namespace {

// spec values for k. reset() reuses kDefaultResults so it can't drift from startup
constexpr int kMinResults = 1;
constexpr int kMaxResults = 100;
constexpr int kDefaultResults = 3;

QLabel* make_heading(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}

}  // namespace

SearchWidget::SearchWidget(QWidget* parent) : QWidget(parent) {
    // query row
    query_edit_ = new QLineEdit(this);
    query_edit_->setPlaceholderText("Search the current corpus");

    result_count_spin_ = new QSpinBox(this);
    result_count_spin_->setRange(kMinResults, kMaxResults);
    result_count_spin_->setValue(kDefaultResults);

    search_button_ = new QPushButton("Search", this);

    auto* query_row = new QHBoxLayout;
    query_row->addWidget(make_heading("Query:", this));
    query_row->addWidget(query_edit_, 1);
    query_row->addSpacing(12);
    query_row->addWidget(new QLabel("Results:", this));
    query_row->addWidget(result_count_spin_);
    query_row->addWidget(search_button_);

    // results table (model/view). the view does NOT take ownership of its
    // model, so the model is parented to this widget instead and dies with it
    result_model_ = new SearchResultModel(this);
    results_view_ = new QTableView(this);
    results_view_->setModel(result_model_);

    // read-only, whole-row, one-at-a-time selection: a "selected result" is
    // always exactly one SearchResult or none
    results_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    results_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    results_view_->setSelectionMode(QAbstractItemView::SingleSelection);
    results_view_->setWordWrap(false);
    results_view_->verticalHeader()->setVisible(false);  // Rank already numbers the rows

    // fixed starting widths for the short columns, Text takes whatever is left
    results_view_->horizontalHeader()->setStretchLastSection(true);
    results_view_->setColumnWidth(0, 50);   // Rank
    results_view_->setColumnWidth(1, 220);  // Document
    results_view_->setColumnWidth(2, 60);   // Chunk
    results_view_->setColumnWidth(3, 80);   // Score

    // selected result
    view_source_button_ = new QPushButton("View Source", this);
    selected_result_text_ = new QPlainTextEdit(this);
    selected_result_text_->setReadOnly(true);
    selected_result_text_->setPlaceholderText("Select a result to inspect its details.");

    auto* selected_header = new QHBoxLayout;
    selected_header->addWidget(make_heading("Selected Result:", this));
    selected_header->addStretch();
    selected_header->addWidget(view_source_button_);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(query_row);
    layout->addWidget(results_view_, 3);
    layout->addLayout(selected_header);
    layout->addWidget(selected_result_text_, 2);

    // local events
    connect(search_button_, &QPushButton::clicked, this, &SearchWidget::request_search);

    // Enter in the query box is the same request as clicking Search.
    // request_search checks the button state, so Enter can't bypass a
    // disabled Search
    connect(query_edit_, &QLineEdit::returnPressed, this, &SearchWidget::request_search);

    // signal -> signal: every edit goes straight out as query_changed so
    // MainWindow can update which buttons are enabled. it never searches
    connect(query_edit_, &QLineEdit::textChanged, this, &SearchWidget::query_changed);

    // selectionModel() only exists after setModel(), so this connect has to
    // come after it or it would connect to a null pointer
    connect(results_view_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &SearchWidget::show_selected_result);

    connect(view_source_button_, &QPushButton::clicked,
            this, &SearchWidget::request_source_document);

    // startup state: no corpus yet, nothing selected
    set_search_enabled(false);
    view_source_button_->setEnabled(false);
}

// exactly as typed (no trimming). MainWindow converts with toStdString()
// when it calls the backend
QString SearchWidget::query() const { return query_edit_->text(); }

int SearchWidget::result_count() const { return result_count_spin_->value(); }

// "   " is not a query. this is what Search and Build Context availability
// is based on
bool SearchWidget::has_query() const { return !query().trimmed().isEmpty(); }

void SearchWidget::set_search_enabled(bool enabled) { search_button_->setEnabled(enabled); }

// clear first so no old selection/details survive, then hand the vector to
// the model. by value + std::move, so the results are never copied
void SearchWidget::set_results(std::vector<SearchResult> results) {
    clear_results();
    result_model_->set_results(std::move(results));
}

// a model reset doesn't reliably fire selectionChanged, so the "no chunk
// selected" announcement is made here directly. otherwise the Term Inspector
// could keep a chunk id from results that no longer exist
void SearchWidget::clear_results() {
    results_view_->clearSelection();
    result_model_->clear();
    selected_result_text_->clear();
    view_source_button_->setEnabled(false);
    emit selected_chunk_changed(QString());
}

// Resettable::reset override, reached through virtual dispatch from MainWindow
void SearchWidget::reset() {
    query_edit_->clear();
    result_count_spin_->setValue(kDefaultResults);
    clear_results();
    set_search_enabled(false);
}

// the widget only reports the request; MainWindow does the actual search
void SearchWidget::request_search() {
    if (!search_button_->isEnabled() || !has_query()) return;
    emit search_requested(query(), result_count());
}

// display only: reads the SearchResult already stored in the model, so
// selecting a row never runs Search again
void SearchWidget::show_selected_result() {
    const auto rows = results_view_->selectionModel()->selectedRows();
    const SearchResult* r = rows.isEmpty() ? nullptr : result_model_->result_at(rows.first().row());

    if (!r) {
        selected_result_text_->clear();
        view_source_button_->setEnabled(false);
        emit selected_chunk_changed(QString());
        return;
    }

    selected_result_text_->setPlainText(
        QString("Rank: %1\nDocument: %2\nChunk: %3 (id: %4)\nScore: %5\nMatched terms: %6\n\nText:\n%7")
            .arg(rows.first().row() + 1)
            .arg(QString::fromStdString(r->document_id))
            .arg(static_cast<qulonglong>(r->chunk_sequence))
            .arg(QString::fromStdString(r->chunk_id))
            .arg(r->score)
            .arg(static_cast<qulonglong>(r->matched_terms))
            .arg(QString::fromStdString(r->text)));
    view_source_button_->setEnabled(true);

    // Term Inspector needs the chunk id for TF. it only remembers it, it
    // doesn't inspect until the user clicks Inspect
    emit selected_chunk_changed(QString::fromStdString(r->chunk_id));
}

// hands MainWindow just the document id. finding and showing the document is
// MainWindow's job since it owns the workspace
void SearchWidget::request_source_document() {
    const auto rows = results_view_->selectionModel()->selectedRows();
    if (rows.isEmpty()) return;

    const SearchResult* r = result_model_->result_at(rows.first().row());
    if (!r) return;
    emit source_document_requested(QString::fromStdString(r->document_id));
}

}  // namespace aiws::gui
