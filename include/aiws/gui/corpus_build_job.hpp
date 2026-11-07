#pragma once

#include "aiws/processing_core.hpp"
#include "aiws/workspace.hpp"

#include <atomic>
#include <cstdint>
#include <future>
#include <thread>

class QObject;

namespace aiws::gui {

// Runs one corpus build on its own std::thread. The job builds a brand new
// ProcessingCore from its own copy of the Workspace, so the worker never
// touches MainWindow, its Workspace, its current core, or any widget. When
// the build is done (core or exception) it posts a queued call to the
// receiver's slot, which runs on the GUI thread.
//
// threads: the constructor, cancel(), generation(), take_result() and the
// destructor run on the GUI thread. run() is the only worker-thread code
class CorpusBuildJob final {
public:
    // makes the empty core the build fills in. runs on the worker thread, so
    // it can't touch shared state. null = ProcessingCore{}. tests pass one
    // whose chunking strategy throws, to get a build that fails
    using CoreFactory = ProcessingCore (*)();

    // workspace is taken by value: this copy is the snapshot the worker
    // builds, so Add Document / Reset on the GUI side can't reach it.
    // receiver has to outlive the job (MainWindow owns the job, so it does)
    CorpusBuildJob(Workspace workspace, int delay_ms_per_document, std::uint64_t generation,
                   QObject* receiver, const char* finished_slot, CoreFactory factory = nullptr);

    // cancels if still running, then joins. never leaves a joinable thread
    // behind (destroying one calls std::terminate)
    ~CorpusBuildJob();

    // owns a running thread, so copying or moving it makes no sense
    CorpusBuildJob(const CorpusBuildJob&) = delete;
    CorpusBuildJob& operator=(const CorpusBuildJob&) = delete;
    CorpusBuildJob(CorpusBuildJob&&) = delete;
    CorpusBuildJob& operator=(CorpusBuildJob&&) = delete;

    // asks the worker to stop early. doesn't wait for it
    void cancel() noexcept;

    // the build generation this job was started for. MainWindow compares it
    // to its own to decide if the result still matches the workspace
    std::uint64_t generation() const noexcept;

    // the built core, or rethrows the build's exception. only call it from
    // the finished slot: by then the result is already in the future, so
    // get() returns right away instead of blocking the GUI. one call only
    ProcessingCore take_result();

private:
    // worker thread: delay, build, fill the promise, notify
    void run();

    // set once in the constructor and only read after that
    Workspace workspace_;
    int delay_ms_per_document_;
    std::uint64_t generation_;
    QObject* receiver_;
    const char* finished_slot_;
    CoreFactory factory_;

    // the one piece of mutable data both threads touch while both run:
    // written by the GUI thread (cancel / destructor), read by the worker
    std::atomic<bool> cancel_{false};

    // worker sets it once, the GUI thread reads it once through future_.
    // the standard library synchronizes the shared state between them
    std::promise<ProcessingCore> promise_;
    std::future<ProcessingCore> future_;

    // declared last on purpose: members are initialized in declaration
    // order, so everything above exists before the thread starts run()
    std::thread worker_;
};

}  // namespace aiws::gui
