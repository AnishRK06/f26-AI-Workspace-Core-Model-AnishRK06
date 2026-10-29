# M4 Design Note

## 1. Components and threads

- `MainWindow` (GUI thread): owns `workspace_`, `processing_core_`, `corpus_current_`, and the current build job. Adds a `build_generation_` counter that goes up on every Add Document and Reset. It is the only class that touches widgets.
- `CorpusBuildJob` (new class): runs one build.
  - The constructor, `cancel()`, `take_result()`, and the destructor run on the GUI thread.
  - `run()` runs on its own `std::thread`.
  - It holds a copy of the Workspace, the build delay, the generation it was started for, a `std::atomic<bool> cancel_`, and a `std::promise<ProcessingCore>`.
- Worker thread (`CorpusBuildJob::run()`):
  1. For each document, sleep `delay_ms` in 10 ms steps, stopping early if `cancel_` is set.
  2. Make a new `ProcessingCore` and call `rebuild()` on the copied Workspace.
  3. Put the core, or the exception, into the promise.
  4. Tell the GUI it's done with `QMetaObject::invokeMethod(window, "build_finished", Qt::QueuedConnection)`.

  It never touches widgets or `MainWindow` members.
- Widgets (GUI thread): same as M3, they only show what `MainWindow` tells them.

## 2. Corpus states and events

States:
- NotBuilt: no current corpus and no build running. A failed build ends up here, with the error shown.
- Building: a build is running for the current Workspace.
- Stale: a build is running, but the Workspace changed after it started.
- Current: the corpus matches the present Workspace (`corpus_current_ == true`).

Events:
- Build Corpus clicked:
  - NotBuilt → Building.
  - Current → Building, and the old corpus can still be used since the Workspace didn't change.
  - Build is disabled while a job is running.
- Add Document or Reset: `build_generation_++`, then `invalidate_corpus()`.
  - Building → Stale, and the job is told to cancel.
  - Current → NotBuilt.
- `build_finished()` with a result:
  - If the job's generation equals `build_generation_` (state Building), the core is moved into `processing_core_` and the state becomes Current.
  - Otherwise (state Stale) the result is thrown away and the state becomes NotBuilt.
- `build_finished()` with an exception: Building → NotBuilt and the error is shown. Stale → NotBuilt and nothing is shown.
- Exit: the job is cancelled and joined, then the window closes.

`build_finished()` always joins the thread and deletes the job.

## 3. Event sequences that threaten R1-R4

- Add Document during a build (R2). The old result could be accepted. Fixed by the generation check.
- Reset, then re-adding the same file during a build (R2). The doc count matches, so the result looks valid. Fixed by comparing generations instead of doc counts or contents.
- `rebuild()` throws on the worker (R3/R4). An exception that escapes a thread calls `std::terminate`. The worker catches everything and calls `set_exception`. Then `take_result()` rethrows on the GUI thread and the error is shown.
- Exit during a long build (R4). Destroying a joinable `std::thread` terminates the program, and a long `join()` hangs it. Fixed by setting `cancel_` before `join()`, so the worker stops within about 10 ms.
- Many builds in a row (R4). Threads or memory could pile up. Only one job exists at a time, and it is joined and deleted in `build_finished()`.
- Adding a document while the worker is reading the Workspace (data race). The worker only reads its own copy.
- A build finishing while the Exit confirm dialog is open. `build_finished()` works in any state, so this is fine.

Walkthrough A: Add Document during a build
1. `build_generation_` is 3. Build is clicked, and job J (gen 3) starts with a copy of 2 docs. State: Building.
2. The user adds a doc. The Workspace now has 3 docs, `build_generation_` becomes 4, `invalidate_corpus()` runs, and `J.cancel()` is called. State: Stale.
3. The worker sees `cancel_` (or just finishes) and posts `build_finished`.
4. `build_finished()` joins J and sees 3 != 4, so the result is thrown away. State: NotBuilt, Build is enabled, and no chunk count is shown. Search was disabled the whole time.

Walkthrough B: Exit during a slow build (`--build-delay-ms 5000`, 10 docs)
1. State: Building, and the worker is sleeping in 10 ms steps.
2. Exit is confirmed, and `~MainWindow` destroys the job (`std::unique_ptr<CorpusBuildJob>`).
3. `~CorpusBuildJob` sets `cancel_`. The worker stops within 10 ms and posts, but Qt drops that event because the window is being destroyed.
4. `join()` returns and the thread is gone. No crash and no hang.

## 4. Ownership of one built corpus

1. The worker creates a local `ProcessingCore` and calls `rebuild()`.
2. `promise.set_value(std::move(core))` moves it into the future's shared state.
3. On the GUI thread, `take_result()` calls `future.get()` and moves it into a local in `build_finished()`.
4. If the result is valid, `processing_core_ = std::move(core)` and the old core is destroyed. If it is stale, the local is destroyed at the end of `build_finished()`.
5. The current core is later destroyed by the next accepted build, by Reset (`processing_core_ = ProcessingCore{}`), or by `~MainWindow`.

It is always destroyed on the GUI thread.

## 5. Rejected alternative

Calling `processing_core_.rebuild()` directly from the worker thread.

Why not: Search, Term Inspector, and Context read `processing_core_` on the GUI thread while the worker writes it. That is a data race, and they could see a half-built corpus. Fixing it would need a mutex, which is M5. Building into a separate core and handing it over through a `std::future` means only one thread uses each core at a time.
