#pragma once

#include "aiws/processing_core.hpp"
#include "aiws/workspace.hpp"

#include <QObject>
#include <QString>

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace aiws::gui {

// makes the empty core a build fills in. runs on the worker thread, so it
// can't touch shared state. null = ProcessingCore{}. tests pass one whose
// chunking strategy throws, to get a build that fails
using CoreFactory = ProcessingCore (*)();

// everything one build needs. the GUI thread fills it in and hands it over,
// and from then on only the worker uses it
struct BuildRequest {
    Workspace workspace;  // a copy (snapshot), never MainWindow's workspace_
    std::uint64_t generation{0};
    int delay_ms_per_document{0};
    CoreFactory factory{nullptr};
};

// what comes back from a build. travels to the GUI thread inside a queued
// signal, so Qt's event queue is what moves it between threads. shared_ptr
// only so it's copyable (queued signal arguments get copied)
struct BuildResult {
    std::uint64_t generation{0};
    std::shared_ptr<ProcessingCore> core;  // the built corpus, null if it failed
    QString error;                         // why it failed, empty if it didn't
    std::thread::id built_on;              // which thread ran it (R1 tests)
};

// The one build thread for the whole app (R1). It starts in the constructor
// and runs every build until shutdown(). In between it sleeps on cv_, so it
// uses no CPU while there's nothing to build. Replaces M4's CorpusBuildJob,
// which made a new thread for every build.
//
// threads: the constructor, submit(), set_wanted_generation(), shutdown()
// and the destructor run on the GUI thread. run() and build() are the only
// worker-thread code. the two signals are emitted on the worker thread, but
// they're connected queued, so the slots run on the GUI thread
class BuildWorker final : public QObject {
    Q_OBJECT

public:
    explicit BuildWorker(QObject* parent = nullptr);

    // shutdown(). never leaves a joinable thread behind
    ~BuildWorker() override;

    // owns a running thread, so copying or moving it makes no sense
    BuildWorker(const BuildWorker&) = delete;
    BuildWorker& operator=(const BuildWorker&) = delete;
    BuildWorker(BuildWorker&&) = delete;
    BuildWorker& operator=(BuildWorker&&) = delete;

    // R2: puts the request in the one pending slot and wakes the worker.
    // returns right away, never waits for a build. a newer request just
    // replaces an older one that wasn't taken yet
    void submit(BuildRequest request);

    // R2/R3: only builds for this generation are still useful. a request
    // for any other one is skipped, and a running one stops at its next
    // check. called on every Add Document, remote add and Reset
    void set_wanted_generation(std::uint64_t generation);

    // R4: tells the worker to stop, wakes it and joins it. a running build
    // stops within one document's delay. safe to call more than once
    void shutdown() noexcept;

    // the worker thread's id, so tests can check every build ran on it
    std::thread::id thread_id() const noexcept;

signals:
    // one of these two for every build that wasn't cancelled. two signals
    // instead of one, so "finished" and "failed" reach MainWindow as two
    // separate events and nothing has to check which one it got
    void build_finished(aiws::gui::BuildResult result);
    void build_failed(aiws::gui::BuildResult result);

private:
    // worker thread: wait for a request (or stop), build it, repeat
    void run();

    // worker thread: one build. returns early if it stops being wanted
    void build(const BuildRequest& request);

    // true if the build for generation should stop. caller holds mutex_
    bool should_stop(std::uint64_t generation) const;

    // ---- shared between the GUI thread and the worker. all three are only
    // read or written with mutex_ locked, and every change is followed by a
    // notify on cv_, so the worker can't miss one while it's waiting
    std::mutex mutex_;
    std::condition_variable cv_;
    std::optional<BuildRequest> pending_;  // next build, empty if none
    std::uint64_t wanted_generation_{0};  // builds for anything else stop
    bool stop_{false};                    // set once, by shutdown()

    // GUI thread only. set in the constructor and only read after that
    std::thread::id worker_id_;

    // declared last on purpose: members are initialized in declaration
    // order, so everything above exists before the thread starts run()
    std::thread worker_;
};

}  // namespace aiws::gui
