# M5 Decision Record

## 1. M4 fixes

- `IpcServer` called `std::strerror` on both the GUI and IPC threads (shared static buffer). Now `error_text()` uses `strerror_r`.
- The IPC thread woke every 100 ms to check `stop_`. It now sleeps in `poll()` with no timeout, and `IpcServer::stop()` wakes it through a pipe (constraint 3).

## 2. Threads and shared data

Three threads: **GUI** (every `MainWindow` slot, all state objects, `BuildWorker::submit / set_wanted_generation / shutdown`), **build worker** (`BuildWorker::run()`, `build()`), and **IPC** (`IpcServer::run()`, `serve_client()`).

| Shared mutable data | Threads | Why it's safe |
|---|---|---|
| `BuildWorker::pending_`, `wanted_generation_`, `stop_` | GUI writes, worker reads | only touched with `mutex_` locked; every change is followed by `cv_.notify`, and the worker waits with a predicate, so no wakeup is lost |
| `BuildResult` | worker → GUI | emitted through a queued signal: Qt copies it into its locked event queue |
| IPC wake pipe | GUI writes, IPC reads | kernel object; opened before the thread starts, closed after `join()` |

The request's workspace is a copy moved out under the lock; then only the worker uses it. ThreadSanitizer: no races.

## 3. States

| State | Build | Workspace changed | Reset | Finished | Failed |
|---|---|---|---|---|---|
| `NotBuiltState` (start) | `start_build()` → Building | stay | stay | ignore | ignore |
| `BuildingState` | ignore | `cancel_build()` → NotBuilt | `cancel_build()` → NotBuilt | `use_corpus()` → Current | `show_build_failed()` → Failed |
| `CurrentState` | `start_build()` → Building (old corpus stays usable) | → NotBuilt | → NotBuilt | ignore | ignore |
| `FailedState` | `start_build()` → Building | → NotBuilt | → NotBuilt | ignore | ignore |

Each event is a virtual function returning the next state (null = stay); `MainWindow::apply()` swaps it in after the call returns. States only see `CorpusContext`.

## 4. Traced sequence: Reset during a long build

1. GUI: `reset_application()` bumps `build_generation_` to g+1 and clears the workspace.
2. GUI: `apply(state_->on_reset(*this))` → `BuildingState::on_reset()` → `MainWindow::cancel_build()` → `BuildWorker::set_wanted_generation(g+1)` locks `mutex_`, sets it, unlocks, `notify_all()`. It returns `NotBuiltState`; Build is enabled again.
3. Worker: in `build()`, `cv_.wait_for()` wakes, `should_stop(g)` is true, so it returns without emitting and goes back to `cv_.wait()` in `run()`.
4. If the result was emitted just before step 2, `MainWindow::build_finished()` sees generation g ≠ g+1 and drops it.

## 5. Decisions

1. **Build during a build:** not allowed. `BuildingState::build_allowed()` is false, so `update_action_states()` disables the button, and `BuildingState::on_build_requested()` ignores it.
2. **Remote add mid-build:** same as a local add. `remote_add_requested()` → `add_document_file()` → `BuildingState::on_workspace_changed()` cancels the build. The user sees "Out of date" and "The running build was cancelled", and rebuilds manually.
3. **Shared data:** `pending_`, `wanted_generation_`, `stop_` under one mutex. Not smaller: the worker must learn three separate things (new work, a cancel, exit), and the result travels in Qt's event queue.

## 6. Fault switches

| Macro | Location | Caught by |
|---|---|---|
| `AIWS_FAULT_NO_CANCEL` | `BuildWorker::should_stop()` | `test_reset_stops_long_build`, `test_add_stops_long_build`, `test_unwanted_builds_never_built` |
| `AIWS_FAULT_SILENT_SHUTDOWN` | `BuildWorker::shutdown()` (skips `notify_all`) | `test_exit_with_idle_worker_never_hangs` (watchdog, fails in 2 s) |

## 7. AI-use disclosure

I used Claude throughout M5. It reviewed my M4 against the M5 spec, proposed the design and assisted me in development checking for bugs ands builds. 
