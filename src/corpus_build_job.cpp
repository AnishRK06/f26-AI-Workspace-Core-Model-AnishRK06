#include "aiws/gui/corpus_build_job.hpp"

#include <QMetaObject>
#include <QObject>

#include <algorithm>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <utility>

namespace aiws::gui {
namespace {

// how often the simulated delay checks cancel_. small enough that Add,
// Reset or Exit during a long --build-delay-ms build stops it almost at once
constexpr int kCancelCheckMs = 10;

}  // namespace

// promise_ is declared before future_, so it exists by the time get_future()
// runs. worker_ is declared last, so the thread starts after everything else
CorpusBuildJob::CorpusBuildJob(Workspace workspace, int delay_ms_per_document, std::uint64_t generation,
                               QObject* receiver, const char* finished_slot, CoreFactory factory)
    : workspace_(std::move(workspace)),
      delay_ms_per_document_(std::max(0, delay_ms_per_document)),
      generation_(generation),
      receiver_(receiver),
      finished_slot_(finished_slot),
      factory_(factory),
      future_(promise_.get_future()),
      worker_(&CorpusBuildJob::run, this) {}

// a joinable std::thread being destroyed calls std::terminate, so this always
// joins. cancel first so a long delay doesn't turn the join into a hang (R4)
CorpusBuildJob::~CorpusBuildJob() {
    cancel();
    if (worker_.joinable()) worker_.join();
}

void CorpusBuildJob::cancel() noexcept { cancel_ = true; }

std::uint64_t CorpusBuildJob::generation() const noexcept { return generation_; }

// the finished slot only runs after run() filled the promise, so get() never
// actually waits here. it rethrows whatever the worker caught
ProcessingCore CorpusBuildJob::take_result() { return future_.get(); }

void CorpusBuildJob::run() {
    try {
        // --build-delay-ms: about N ms more per document, on this thread, never
        // the GUI. sliced so cancel_ is noticed within kCancelCheckMs
        const auto docs = workspace_.documents().size();
        for (std::size_t i = 0; i < docs; ++i) {
            for (int waited = 0; waited < delay_ms_per_document_; waited += kCancelCheckMs) {
                if (cancel_) throw std::runtime_error("build cancelled");
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(std::min(kCancelCheckMs, delay_ms_per_document_ - waited)));
            }
        }
        if (cancel_) throw std::runtime_error("build cancelled");

        // a fresh core, built from our own copy. nobody else can see it
        // until the GUI thread takes it out of the future
        ProcessingCore core = factory_ ? factory_() : ProcessingCore{};
        core.rebuild(workspace_);
        promise_.set_value(std::move(core));
    } catch (...) {
        // an exception escaping a std::thread calls std::terminate, so
        // everything is caught here and handed to the GUI through the future.
        // a cancelled build ends up here too; MainWindow only cancels builds
        // it's already going to throw away, so it never shows that one
#ifdef AIWS_FAULT_LOSE_FAILURE
        // fault: the failure never reaches the GUI, so it stays "Building" (R3)
        return;
#endif
        promise_.set_exception(std::current_exception());
    }

    // result is in the future now, so the slot can take it without waiting.
    // queued: runs later on the GUI thread, this thread never waits for it
    QMetaObject::invokeMethod(receiver_, finished_slot_, Qt::QueuedConnection);
}

}  // namespace aiws::gui
