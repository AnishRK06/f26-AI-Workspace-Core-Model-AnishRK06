#include "aiws/gui/search_result_model.hpp"

#include <QString>

#include <utility>

namespace aiws::gui {
namespace {

// column order lives in one place so data() and headerData() can't disagree
// about which column is which
enum Column : int { kRank = 0, kDocument, kChunk, kScore, kText, kColumnCount };

}  // namespace

// the model owns no Qt widgets, only the results_ vector. the parent is just
// for Qt cleanup (SearchWidget passes itself, so the model dies with it)
SearchResultModel::SearchResultModel(QObject* parent) : QAbstractTableModel(parent) {}

// model/view: the QTableView stores nothing. every repaint it asks the model
// how many rows/columns exist and what goes in each cell, so these answers
// are the only source of truth for the table
//
// flat table: a valid parent would mean "children of a cell", which a table
// doesn't have, so only the invisible root gets rows
int SearchResultModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(results_.size());
}

int SearchResultModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : kColumnCount;
}

// the view asks for many roles per cell (text, font, color, alignment...).
// only DisplayRole is answered here; an empty QVariant tells the view to use
// its default for everything else
//
// QVariant is a box that can hold an int, a QString or a double, which is how
// one function returns a different type per column
QVariant SearchResultModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || role != Qt::DisplayRole) return {};

    const SearchResult* r = result_at(index.row());
    if (!r) return {};

    switch (index.column()) {
    case kRank: return index.row() + 1;  // rank is the 1-based row position
    case kDocument: return QString::fromStdString(r->document_id);
    // QVariant has no std::size_t constructor, qulonglong is the closest fit
    case kChunk: return static_cast<qulonglong>(r->chunk_sequence);
    case kScore: return r->score;
    case kText: return QString::fromStdString(r->text);
    default: return {};
    }
}

// only horizontal (column) headers; the row numbers on the left are hidden
// by the view since Rank already covers that
QVariant SearchResultModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return {};

    switch (section) {
    case kRank: return QString("Rank");
    case kDocument: return QString("Document");
    case kChunk: return QString("Chunk");
    case kScore: return QString("Score");
    case kText: return QString("Text");
    default: return {};
    }
}

// begin/endResetModel tell every attached view "forget what you knew and ask
// me again", so the table redraws itself without SearchWidget touching it.
// skipping them would leave the view showing stale rows (or reading past the
// end of the new vector)
//
// results is taken by value, so the caller's std::move already handed the
// vector over. moving it into results_ means no copy ever happens
void SearchResultModel::set_results(std::vector<SearchResult> results) {
    beginResetModel();
    results_ = std::move(results);
    endResetModel();
}

void SearchResultModel::clear() {
    beginResetModel();
    results_.clear();
    endResetModel();
}

// nullptr instead of throwing: "no row selected" (-1) is a normal case, not
// an error. the pointer is only valid until the next set_results/clear, so
// callers use it right away and never keep it
const SearchResult* SearchResultModel::result_at(int row) const noexcept {
    return row < 0 || static_cast<std::size_t>(row) >= results_.size()
               ? nullptr
               : &results_[static_cast<std::size_t>(row)];
}

}  // namespace aiws::gui
