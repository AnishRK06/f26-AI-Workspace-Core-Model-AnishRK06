#include "aiws/gui/context_widget.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QStringList>
#include <QVBoxLayout>

namespace aiws::gui {
namespace {

// spec values for the token budget. reset() reuses kDefaultBudget so it can't
// drift from startup
constexpr int kMinBudget = 50;
constexpr int kMaxBudget = 1000;
constexpr int kBudgetStep = 50;
constexpr int kDefaultBudget = 300;

}  // namespace

ContextWidget::ContextWidget(QWidget* parent) : QWidget(parent) {
    auto* heading = new QLabel("Context:", this);
    QFont bold = heading->font();
    bold.setBold(true);
    heading->setFont(bold);

    // singleStep/pageStep only cover arrow keys and clicking the track.
    // dragging can still land anywhere, which budget_changed snaps back to 50s
    context_budget_slider_ = new QSlider(Qt::Horizontal, this);
    context_budget_slider_->setRange(kMinBudget, kMaxBudget);
    context_budget_slider_->setSingleStep(kBudgetStep);
    context_budget_slider_->setPageStep(kBudgetStep);

    // wide enough for "1000" so the row doesn't shift while dragging
    budget_value_label_ = new QLabel(this);
    budget_value_label_->setMinimumWidth(budget_value_label_->fontMetrics().horizontalAdvance("0000"));
    budget_value_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    build_context_button_ = new QPushButton("Build Context", this);

    context_text_ = new QPlainTextEdit(this);
    context_text_->setReadOnly(true);
    context_text_->setPlaceholderText("Built context will appear here.");

    auto* budget_row = new QHBoxLayout;
    budget_row->addWidget(new QLabel("Token budget:", this));
    budget_row->addWidget(context_budget_slider_, 1);
    budget_row->addWidget(budget_value_label_);
    budget_row->addWidget(build_context_button_);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(heading);
    layout->addLayout(budget_row);
    layout->addWidget(context_text_, 1);

    // local events. moving the slider only updates the number, it never
    // builds context on its own
    connect(context_budget_slider_, &QSlider::valueChanged, this, &ContextWidget::budget_changed);
    connect(build_context_button_, &QPushButton::clicked, this, &ContextWidget::request_context);

    // startup state is the same as the reset state, so reuse it
    reset();
}

int ContextWidget::token_budget() const { return context_budget_slider_->value(); }

void ContextWidget::set_build_context_enabled(bool enabled) { build_context_button_->setEnabled(enabled); }

// one block per ContextItem with the six fields the spec lists. truncated is
// shown exactly as the backend set it, the GUI never decides truncation.
// an empty vector is a valid result, so it gets a message instead of a blank
void ContextWidget::set_context(const std::vector<ContextItem>& items) {
    if (items.empty()) {
        context_text_->setPlainText("No context items were returned.");
        return;
    }

    QStringList blocks;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const ContextItem& c = items[i];
        blocks << QString("[%1]\nDocument: %2\nChunk: %3\nTokens: %4\nScore: %5\nTruncated: %6\nText: %7")
                      .arg(static_cast<qulonglong>(i + 1))
                      .arg(QString::fromStdString(c.document_id))
                      .arg(static_cast<qulonglong>(c.chunk_sequence))
                      .arg(static_cast<qulonglong>(c.token_count))
                      .arg(c.score)
                      .arg(c.truncated ? "yes" : "no")
                      .arg(QString::fromStdString(c.text));
    }
    context_text_->setPlainText(blocks.join("\n\n"));
}

void ContextWidget::clear_context() { context_text_->clear(); }

// Resettable::reset override, reached through virtual dispatch from MainWindow.
// budget_changed is called directly because setValue(300) emits nothing when
// the slider is already at 300, and the label still has to be set at startup
void ContextWidget::reset() {
    context_budget_slider_->setValue(kDefaultBudget);
    budget_changed(kDefaultBudget);
    clear_context();
    set_build_context_enabled(false);
}

// the widget only reports the request with the current budget. query and k
// come from SearchWidget, so MainWindow puts the call together
void ContextWidget::request_context() { emit build_context_requested(token_budget()); }

// snap to the nearest multiple of 50. setValue re-enters this slot once with
// the snapped value, and that second call is the one that updates the label
void ContextWidget::budget_changed(int value) {
    const int snapped = (value + kBudgetStep / 2) / kBudgetStep * kBudgetStep;
    if (snapped != value) {
        context_budget_slider_->setValue(snapped);
        return;
    }
    budget_value_label_->setText(QString::number(value));
}

}  // namespace aiws::gui
