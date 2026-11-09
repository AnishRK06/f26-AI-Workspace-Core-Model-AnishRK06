# M4 Decision Record

## 1. Threads and ownership

| Thread | Runs |
|---|---|
| GUI | every `MainWindow` slot and widget; `CorpusBuildJob` and `IpcServer` construction/destruction, `take_result()`, `stop()` |
| Build worker (`CorpusBuildJob::worker_`) | `CorpusBuildJob::run()` |
| IPC worker (`IpcServer::worker_`) | `IpcServer::run()`, `serve_client()` |

`MainWindow` owns `build_job_` and `ipc_server_` (`std::unique_ptr`). `~CorpusBuildJob` sets `cancel_` and joins, and `IpcServer::stop()` sets `stop_`, joins and unlinks the socket. So `~MainWindow` joins both threads, and nothing is detached. **A built corpus** is created in `run()`, moved into `promise_`, taken by `build_finished()` through `take_result()`, then moved into `processing_core_` or destroyed, both on the GUI thread.

**Constraint 5:**

| Data | Writer | Reader | Safe because |
|---|---|---|---|
| `CorpusBuildJob::cancel_` | GUI | build worker | `std::atomic<bool>` |
| `IpcServer::stop_` | GUI | IPC worker | `std::atomic<bool>` |
| `promise_`/`future_` state | build worker | GUI | the standard library synchronizes it, and `build_finished()` is posted after the promise is set, so `get()` never blocks |

Everything else a worker uses is a copy (the `workspace_` snapshot) or set before the thread starts and never changed.

## 2. How a finished build is judged valid

`add_document_file()` and `reset_application()` increment `build_generation_`. `build_finished()` accepts a result only if `job->generation() == build_generation_`. Otherwise it calls `invalidate_corpus()` and drops the result. No failure is shown, just a "discarded" status line. A document count wouldn't work, because Reset plus re-adding the same file gives the same count.

## 3. Changes since the checkpoint

- Add and Reset no longer cancel the build. A cancelled build became an exception and skipped the generation check, which hid the stale-accept fault. Cancel is now only used on exit.
- Added `CoreFactory` / `set_core_factory()`, so tests can make a build fail.
- Build Corpus is disabled while a build runs (FAQ decision).
- `add_document()` now just opens the dialog and calls `add_document_file()`. Added `is_corpus_current()`, `is_building()` and `document_count()` for tests.

## 4. CR-1 impact

New: `ipc_protocol`, `IpcServer`, `tools/aiws_send.cpp`, `start_remote_add()`, `remote_add_requested()`, and `--ipc-socket`. The build path is unchanged: `remote_add_requested()` calls `add_document_file()`, so R2 applies as is.

| | Byte 0 | Bytes 1-4 (big-endian) | Then |
|---|---|---|---|
| Request | `0x01` add | N (1-65536) | N bytes of UTF-8 absolute path |
| Reply | `0x00` ok / `0x01` rejected | M | M bytes of UTF-8 reason |

The length prefix handles E2 without escaping, and `read_exact()` handles E3. `serve_client()` checks the file itself (`open` + `S_ISREG`), posts the path with a queued `invokeMethod`, then replies, so it never waits for the GUI. Waits are `poll()` calls of at most 100 ms that check `stop_` (E4). Bad `aiws_send` usage exits 1.

## 5. Fault switches

| Macro | Location | Caught by |
|---|---|---|
| `AIWS_FAULT_ACCEPT_STALE` | `MainWindow::build_finished()` | `test_add_during_build_discards_result`, `test_reset_and_readd_during_build_discards_result`, `test_remote_add_during_build_discards_result` |
| `AIWS_FAULT_LOSE_FAILURE` | `catch` in `CorpusBuildJob::run()` | `test_failed_build_is_reported` |

## 6. AI-use disclosure

## AI Use Disclosure

I used AI as an assistant to review the M4 requirements, talk through design decisions, and help debug parts of my implementation and tests. I reviewed and edited all of the submitted code myself, then built and tested it on my own to confirm it works as intended.

---

## Pair programming log

<!-- TODO (you): fill in from your real sessions. -->

**Session 1: November 6, 2026, 9:15 AM–10:00 AM.** We worked on Mustaali's M4 code, mainly the background build, stale-generation handling, and CR-1 remote add behavior. Mustaali drove from 9:15 AM to 9:40 AM, walking through `build_corpus()`, `run_build()`, `build_finished()`, and how Workspace changes invalidate a running build. We swapped roles at 9:40 AM, and I drove from 9:40 AM to 10:00 AM while Mustaali navigated through the related tests and fault behavior. My navigator contribution was reviewing the stale-build path and reminding Mustaali that a remote add during an active build needed to invalidate the old build the same way a GUI add does.

**Session 2: November 6, 2026, 10:15 AM–11:00 AM.** We worked on my M4 code, mainly my background-build and CR-1 integration. I drove from 10:15 AM to 10:20 AM while Mustaali navigated and reviewed the thread lifecycle, generation checking, and stale-result handling. We swapped roles at 10:20 AM, and Mustaali drove from 10:20 AM to 11:00 AM while we reviewed my tests and integration behavior. Mustaali's navigator contribution was pointing out that I needed to handle and test the case where the Workspace changes while a corpus build is still running.

The interaction that worked best for us was having the driver explain each change while the navigator followed the control flow and called out edge cases before moving on.
