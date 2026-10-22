# M3 - Qt AI Workspace

Read the M3 specification before coding. M3 is cumulative: the M1/M2 classes in this repo stay the backend, and M3 puts an event-driven Qt desktop interface around selected parts of them.

## M3 in one view

M3 adds a GUI layer, not new processing. You will:

1. build one focused workflow: add/view documents, build the corpus, search, inspect results and their source documents, inspect term statistics, and build context;
2. present search results through Qt Model/View (`SearchResultModel` + `QTableView`);
3. connect events with signals/slots (no lambdas): component slot -> component signal -> `MainWindow` slot -> backend -> component update;
4. keep application state consistent, so Search, Term Inspector, and Context only ever use a corpus that is current for the present Workspace;
5. reset the major components polymorphically through the `Resettable` interface; and
6. route Exit through a function-pointer `ExitCallback` that asks for confirmation with `QMessageBox`.

Do not reimplement document storage, chunking, retrieval, or context construction inside the GUI.

## Workflow

```
Add documents -> select/view a document -> Build Corpus -> Search
              -> select/view a result -> View Source / Inspect Term -> Build Context
```

Adding a document changes the Workspace, so the old corpus becomes stale and its output is cleared until Build Corpus is clicked again. Changing an input (query, k, token budget, a selection) is never the same as requesting an operation.

## Build and run

```bash
mkdir build && cd build
cmake ..
make
ctest --output-on-failure
./ai_workspace_m3
```

Qt 6 (`qt6-base-dev`) is required; the `Dockerfile` installs it and forwards the display to the host X server.

## Project layout

- `src/` + `include/aiws/` - M1/M2 backend, built as the `aiws_m2` library
- `include/aiws/gui/` - required M3 GUI headers (fixed API contract)
- `src/main.cpp`, `src/main_window.cpp`, `src/workspace_widget.cpp`, `src/search_widget.cpp`, `src/search_result_model.cpp`, `src/term_inspector_widget.cpp`, `src/context_widget.cpp` - M3 implementation files, built as `ai_workspace_m3`
- `data/public/` - public sample documents and queries
- `tests/` - M2 public and student tests (still built and run)

## Required headers

The headers under `include/aiws/gui/` define the M3 API used for grading and must not be modified. Helper files or non-member helpers in `.cpp` files may be added if useful, as long as the required interfaces stay unchanged and any added files are included in the build.

| Component | Responsibility |
| --- | --- |
| `MainWindow` | Owns `Workspace` and `ProcessingCore`; coordinates backend operations and application-wide state |
| `WorkspaceWidget` | Document list, Add Document, Build Corpus, corpus status, selected-document display |
| `SearchWidget` | Query/k controls, Search, results table, selected-result details, View Source |
| `SearchResultModel` | Adapts `std::vector<SearchResult>` to `QTableView` |
| `TermInspectorWidget` | Term input and DF / selected-chunk TF display |
| `ContextWidget` | Token-budget slider, Build Context, context display |
| `Resettable` | Abstract interface used to reset the major components polymorphically |

## Keep M3 focused

No document removal/editing, strategy configuration, chunk browsing, sorting/filtering, tabs, menus/toolbars, drag-and-drop, multiple windows, IPC, threads, or background processing.

## AI-assisted development

AI assistance is permitted. You remain responsible for understanding all submitted code and design decisions.
