#include "aiws/gui/build_worker.hpp"

#include <chrono>
#include <cstddef>
#include <exception>
#include <memory>
#include <utility>

namespace aiws::gui {

// the one build thread starts here and lives until shutdown() (R1)
BuildWorker::BuildWorker(QObject* parent) : QObject(parent), thread_(&BuildWorker::run, this) {
    thread_id_ = thread_.get_id();
}

BuildWorker::~BuildWorker() { shutdown(); }

void BuildWorker::submit(BuildRequest request) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        wanted_generation_ = request.generation;
        pending_ = std::move(request);  // replaces one that wasn't taken yet
    }
    cv_.notify_one();
}

// notify_all, not notify_one: the worker might be waiting in run() for a
// request or in build() out its document delay, and either has to recheck
void BuildWorker::set_wanted_generation(std::uint64_t generation) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        wanted_generation_ = generation;
    }
    cv_.notify_all();
}

// stop_ is set under the lock and then notified, so the worker either sees
// it before it waits or gets woken by the notify. then join: the worker
// returns from run() at its next check, within one document's delay (R4)
void BuildWorker::shutdown() {
    if (!thread_.joinable()) return;  // already shut down
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
#ifndef AIWS_FAULT_SILENT_SHUTDOWN
    cv_.notify_all();
#endif
    // fault: no notify. a worker asleep in run() waiting for a request never
    // wakes up to see stop_, so this join hangs. mid-build it still stops,
    // because build() rechecks stop_ after each document's delay (R4)
    thread_.join();
}

std::thread::id BuildWorker::thread_id() const noexcept { return thread_id_; }

// the cancel check, used by build() between documents and before handing
// the result over. the one place that decides if a build is still wanted
bool BuildWorker::should_stop(std::uint64_t generation) const {
#ifdef AIWS_FAULT_NO_CANCEL
    // fault: only Exit stops a build. one that's no longer wanted runs to the
    // end, and the next request waits in pending_ until it's done (R3)
    (void)generation;
    return stop_;
#else
    return stop_ || wanted_generation_ != generation;
#endif
}

void BuildWorker::run() {
    for (;;) {
        BuildRequest request;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            // sleeps until there's a request or it's time to stop. the
            // predicate is rechecked on every wakeup, spurious ones included
            cv_.wait(lock, [this] { return stop_ || pending_.has_value(); });
            if (stop_) return;

            request = std::move(*pending_);
            pending_.reset();

            // R2: Add or Reset happened after this was submitted, so the
            // workspace it's a snapshot of is gone. never built
            if (request.generation != wanted_generation_) continue;
        }
        // the lock is released here, so submit() and friends never wait for
        // a build, only for the few lines above
        build(request);
    }
}

void BuildWorker::build(const BuildRequest& request) {
    BuildResult result;
    result.generation = request.generation;
    result.built_on = std::this_thread::get_id();

    // --build-delay-ms: the simulated build time, about N ms per document.
    // this is the only sleep, and it's a wait on cv_, so a cancel or Exit
    // wakes it right away instead of after the full delay (R3, R4)
    const auto delay = std::chrono::milliseconds(request.delay_ms_per_document);
    const std::uint64_t gen = request.generation;
    const std::size_t docs = request.workspace.documents().size();
    for (std::size_t i = 0; i < docs; ++i) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (cv_.wait_for(lock, delay, [this, gen] { return should_stop(gen); })) return;
    }

    // the real build, on the request's own snapshot. nothing shared, so no
    // lock. anything it throws has to be caught here: an exception escaping
    // a std::thread calls std::terminate
    try {
        auto core = std::make_shared<ProcessingCore>(request.factory ? request.factory() : ProcessingCore{});
        core->rebuild(request.workspace);
        result.core = std::move(core);
    } catch (const std::exception& e) {
        result.error = QString::fromUtf8(e.what());
    } catch (...) {
        result.error = "unknown error";
    }

    // last check: a build cancelled while rebuild() ran is dropped here, so
    // it never becomes current (R3). the GUI side still checks the
    // generation too, for one that finishes just before a Reset
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (should_stop(request.generation)) return;
    }

    // queued to the GUI thread. emit never waits for the slot, so Exit can't
    // deadlock with the worker trying to hand over a result
    emit build_finished(std::move(result));
}

}  // namespace aiws::gui
