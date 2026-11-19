#include "aiws/gui/corpus_state.hpp"

#include <utility>

namespace aiws::gui {

// ---- CorpusState defaults ------------------------------------------------

// a result that arrives in any state but Building isn't wanted anymore (the
// workspace changed or Reset happened after it was started), so it's dropped
std::unique_ptr<CorpusState> CorpusState::on_build_finished(CorpusContext&, std::shared_ptr<ProcessingCore>) {
    return nullptr;
}

std::unique_ptr<CorpusState> CorpusState::on_build_failed(CorpusContext&, const QString&) { return nullptr; }

// ---- NotBuiltState -------------------------------------------------------

std::unique_ptr<CorpusState> NotBuiltState::on_build_requested(CorpusContext& context) {
    context.start_build();
    return std::make_unique<BuildingState>(false);
}

// nothing is building and there's no corpus to lose. just refresh the display
std::unique_ptr<CorpusState> NotBuiltState::on_workspace_changed(CorpusContext& context) {
    context.invalidate_corpus();
    return nullptr;
}

std::unique_ptr<CorpusState> NotBuiltState::on_reset(CorpusContext& context) {
    context.invalidate_corpus();
    return nullptr;
}

bool NotBuiltState::corpus_usable() const noexcept { return false; }
bool NotBuiltState::build_allowed() const noexcept { return true; }
bool NotBuiltState::building() const noexcept { return false; }

// ---- BuildingState -------------------------------------------------------

BuildingState::BuildingState(bool old_corpus_still_current) noexcept
    : old_corpus_still_current_(old_corpus_still_current) {}

// decision 1: one build at a time. Build Corpus is disabled in this state,
// so this only happens if something calls the slot anyway. ignored
std::unique_ptr<CorpusState> BuildingState::on_build_requested(CorpusContext&) { return nullptr; }

// R3 + decision 2: the build is for a workspace that no longer exists, so it
// stops now instead of running to the end. a remote add comes through here
// too, so it does exactly what a local add does
std::unique_ptr<CorpusState> BuildingState::on_workspace_changed(CorpusContext& context) {
    context.cancel_build();
    context.invalidate_corpus();
    return std::make_unique<NotBuiltState>();
}

std::unique_ptr<CorpusState> BuildingState::on_reset(CorpusContext& context) {
    context.cancel_build();
    context.invalidate_corpus();
    return std::make_unique<NotBuiltState>();
}

// MainWindow only forwards a result for the generation we're still on, so
// this is the build this state is waiting for
std::unique_ptr<CorpusState> BuildingState::on_build_finished(CorpusContext& context,
                                                              std::shared_ptr<ProcessingCore> core) {
    context.use_corpus(std::move(core));
    return std::make_unique<CurrentState>();
}

std::unique_ptr<CorpusState> BuildingState::on_build_failed(CorpusContext& context, const QString& reason) {
    context.show_build_failed(reason);
    return std::make_unique<FailedState>();
}

bool BuildingState::corpus_usable() const noexcept { return old_corpus_still_current_; }
bool BuildingState::build_allowed() const noexcept { return false; }
bool BuildingState::building() const noexcept { return true; }

// ---- CurrentState --------------------------------------------------------

// rebuilding an unchanged workspace. the corpus we have still matches it,
// so it stays usable while the new one builds
std::unique_ptr<CorpusState> CurrentState::on_build_requested(CorpusContext& context) {
    context.start_build();
    return std::make_unique<BuildingState>(true);
}

// the M3 invariant: the corpus no longer matches, so it can't be used
std::unique_ptr<CorpusState> CurrentState::on_workspace_changed(CorpusContext& context) {
    context.invalidate_corpus();
    return std::make_unique<NotBuiltState>();
}

std::unique_ptr<CorpusState> CurrentState::on_reset(CorpusContext& context) {
    context.invalidate_corpus();
    return std::make_unique<NotBuiltState>();
}

bool CurrentState::corpus_usable() const noexcept { return true; }
bool CurrentState::build_allowed() const noexcept { return true; }
bool CurrentState::building() const noexcept { return false; }

// ---- FailedState ---------------------------------------------------------

std::unique_ptr<CorpusState> FailedState::on_build_requested(CorpusContext& context) {
    context.start_build();
    return std::make_unique<BuildingState>(false);
}

std::unique_ptr<CorpusState> FailedState::on_workspace_changed(CorpusContext& context) {
    context.invalidate_corpus();
    return std::make_unique<NotBuiltState>();
}

std::unique_ptr<CorpusState> FailedState::on_reset(CorpusContext& context) {
    context.invalidate_corpus();
    return std::make_unique<NotBuiltState>();
}

bool FailedState::corpus_usable() const noexcept { return false; }
bool FailedState::build_allowed() const noexcept { return true; }
bool FailedState::building() const noexcept { return false; }

}  // namespace aiws::gui
