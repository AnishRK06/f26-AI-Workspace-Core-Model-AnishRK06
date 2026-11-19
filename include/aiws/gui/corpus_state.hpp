#pragma once

#include "aiws/processing_core.hpp"

#include <QString>

#include <memory>

namespace aiws::gui {

// What a corpus state is allowed to do to the application. MainWindow
// implements it, the same way the widgets implement Resettable, so the
// states never see MainWindow itself, its widgets or the BuildWorker.
//
// threads: GUI thread only. states are never handed to the worker
class CorpusContext {
public:
    virtual ~CorpusContext() = default;

    // snapshots the workspace and hands it to the worker. never waits (R2)
    virtual void start_build() = 0;

    // tells the worker the running (or still pending) build is no longer
    // wanted. it stops within one document's delay (R3)
    virtual void cancel_build() = 0;

    // the finished build becomes the corpus Search, Inspect and Context use
    virtual void use_corpus(std::shared_ptr<ProcessingCore> core) = 0;

    // shows that the build failed and why. the app stays usable
    virtual void show_build_failed(const QString& reason) = 0;

    // takes everything that came from the old corpus off the screen
    virtual void invalidate_corpus() = 0;
};

// One state of the corpus lifecycle (R5, State pattern). Each event is a
// virtual function: the current state decides what the event does and
// returns the state to change to, or nullptr to stay where it is. MainWindow
// just forwards events and swaps in what comes back, so nothing outside these
// classes decides a transition.
//
// a state never replaces itself (MainWindow does, after the call returns),
// so no state object is destroyed while one of its functions is running
class CorpusState {
public:
    virtual ~CorpusState() = default;

    // ---- events -------------------------------------------------------

    // Build Corpus was clicked
    virtual std::unique_ptr<CorpusState> on_build_requested(CorpusContext& context) = 0;

    // Add Document, add_document_file() or a remote add changed the workspace
    virtual std::unique_ptr<CorpusState> on_workspace_changed(CorpusContext& context) = 0;

    // Reset was clicked
    virtual std::unique_ptr<CorpusState> on_reset(CorpusContext& context) = 0;

    // the worker finished the build we're waiting for. only BuildingState
    // is waiting for one, so by default a state ignores it
    virtual std::unique_ptr<CorpusState> on_build_finished(CorpusContext& context,
                                                           std::shared_ptr<ProcessingCore> core);

    // the worker's build threw. same as above, only BuildingState cares
    virtual std::unique_ptr<CorpusState> on_build_failed(CorpusContext& context, const QString& reason);

    // ---- what the state allows (read by MainWindow to enable buttons) ---

    // Search, Term Inspector and Context may use the corpus (the M3 invariant)
    virtual bool corpus_usable() const noexcept = 0;

    // Build Corpus may be clicked (still needs at least one document)
    virtual bool build_allowed() const noexcept = 0;

    // a build was handed to the worker and its result is still wanted
    virtual bool building() const noexcept = 0;
};

// No usable corpus: the app just started, the workspace changed, or Reset.
// the initial state
class NotBuiltState final : public CorpusState {
public:
    std::unique_ptr<CorpusState> on_build_requested(CorpusContext& context) override;
    std::unique_ptr<CorpusState> on_workspace_changed(CorpusContext& context) override;
    std::unique_ptr<CorpusState> on_reset(CorpusContext& context) override;

    bool corpus_usable() const noexcept override;
    bool build_allowed() const noexcept override;
    bool building() const noexcept override;
};

// A build is on the worker. if it was started from CurrentState the old
// corpus still matches the workspace (nothing changed), so it stays usable
// until the new one replaces it
class BuildingState final : public CorpusState {
public:
    explicit BuildingState(bool old_corpus_still_current) noexcept;

    std::unique_ptr<CorpusState> on_build_requested(CorpusContext& context) override;
    std::unique_ptr<CorpusState> on_workspace_changed(CorpusContext& context) override;
    std::unique_ptr<CorpusState> on_reset(CorpusContext& context) override;
    std::unique_ptr<CorpusState> on_build_finished(CorpusContext& context,
                                                   std::shared_ptr<ProcessingCore> core) override;
    std::unique_ptr<CorpusState> on_build_failed(CorpusContext& context, const QString& reason) override;

    bool corpus_usable() const noexcept override;
    bool build_allowed() const noexcept override;
    bool building() const noexcept override;

private:
    // set once when the state is made. only answers corpus_usable(), it
    // never decides a transition
    bool old_corpus_still_current_;
};

// The corpus matches the workspace. Search, Inspect and Context are allowed
class CurrentState final : public CorpusState {
public:
    std::unique_ptr<CorpusState> on_build_requested(CorpusContext& context) override;
    std::unique_ptr<CorpusState> on_workspace_changed(CorpusContext& context) override;
    std::unique_ptr<CorpusState> on_reset(CorpusContext& context) override;

    bool corpus_usable() const noexcept override;
    bool build_allowed() const noexcept override;
    bool building() const noexcept override;
};

// The last build threw. no usable corpus, but Build can be tried again
class FailedState final : public CorpusState {
public:
    std::unique_ptr<CorpusState> on_build_requested(CorpusContext& context) override;
    std::unique_ptr<CorpusState> on_workspace_changed(CorpusContext& context) override;
    std::unique_ptr<CorpusState> on_reset(CorpusContext& context) override;

    bool corpus_usable() const noexcept override;
    bool build_allowed() const noexcept override;
    bool building() const noexcept override;
};

}  // namespace aiws::gui
