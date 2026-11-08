// Student-written tests (M2 backend + M4 responsive GUI + CR-1 remote add)
//
// Groups:
//   1. default configuration still behaves like M1
//   2. custom strategies are actually dispatched to (call counting + output)
//   3. invalid configuration (null strategies)
//   4. ownership / lifetime (destruction counting, move-only, moved-from)
//   5. rebuild across responsibilities + failed-rebuild rollback
//   6. M4 R1/R2: the build runs in the background, and a build that finishes
//      after the workspace changed never becomes current
//   7. M4 R3/R4: a failed build is reported, exit mid-build is quick, builds
//      don't pile up
//   8. CR-1 E1-E4: remote add through the real aiws_send and a raw client
//
// everything in 6-8 waits with a time limit, so a broken build fails here
// within seconds instead of hanging until the 60 s ctest timeout

#include "aiws/chunker.hpp"
#include "aiws/chunking_strategy.hpp"
#include "aiws/context_builder.hpp"
#include "aiws/context_strategy.hpp"
#include "aiws/processing_core.hpp"
#include "aiws/retrieval_engine.hpp"
#include "aiws/retrieval_strategy.hpp"
#include "aiws/gui/main_window.hpp"
#include "aiws/ipc_protocol.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <thread>

#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

using namespace aiws;

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const std::string& name) {
    ++checks;
    if (!ok) {
        std::cerr << "FAIL: " << name << '\n';
        ++failures;
    }
}

template <typename E, typename F>
bool throws(F&& f) {
    try {
        f();
    } catch (const E&) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}

// ---- compile-time contract checks --------------------------------------

// strategies are abstract and safe to delete through a base pointer
static_assert(std::is_abstract<ChunkingStrategy>::value, "ChunkingStrategy abstract");
static_assert(std::is_abstract<RetrievalStrategy>::value, "RetrievalStrategy abstract");
static_assert(std::is_abstract<ContextStrategy>::value, "ContextStrategy abstract");
static_assert(std::has_virtual_destructor<ChunkingStrategy>::value, "virtual dtor");
static_assert(std::has_virtual_destructor<RetrievalStrategy>::value, "virtual dtor");
static_assert(std::has_virtual_destructor<ContextStrategy>::value, "virtual dtor");

// assigning through a base reference would slice, so it is not public
static_assert(!std::is_copy_assignable<ChunkingStrategy>::value, "no slicing assign");
static_assert(!std::is_copy_assignable<RetrievalStrategy>::value, "no slicing assign");
static_assert(!std::is_copy_assignable<ContextStrategy>::value, "no slicing assign");

// the defaults really are strategies
static_assert(std::is_base_of<ChunkingStrategy, Chunker>::value, "Chunker is-a");
static_assert(std::is_base_of<RetrievalStrategy, RetrievalEngine>::value, "Engine is-a");
static_assert(std::is_base_of<ContextStrategy, ContextBuilder>::value, "Builder is-a");

// ProcessingCore is move-only
static_assert(!std::is_copy_constructible<ProcessingCore>::value, "no copy ctor");
static_assert(!std::is_copy_assignable<ProcessingCore>::value, "no copy assign");
static_assert(std::is_nothrow_move_constructible<ProcessingCore>::value, "move ctor");
static_assert(std::is_nothrow_move_assignable<ProcessingCore>::value, "move assign");

// ---- test strategies -----------------------------------------------------

// shared between a test and the strategies it hands to the core. the core
// owns the strategies, the test keeps the Probe alive, so after the core is
// gone the test can still see what happened
struct Probe {
    int chunk_calls = 0;
    int search_calls = 0;
    int build_calls = 0;
    int destroyed = 0;
    int last_k = -1;
    std::size_t last_budget = 0;
};

// one marker chunk per document. text is "marker <id>", so only the word
// "marker" (and the id) is searchable - none of the real document words are
class MarkerChunker final : public ChunkingStrategy {
public:
    explicit MarkerChunker(std::shared_ptr<Probe> p = nullptr) : probe_(std::move(p)) {}
    ~MarkerChunker() override {
        if (probe_) ++probe_->destroyed;
    }
    std::vector<Chunk> chunk(const Document& d, std::size_t order) const override {
        if (probe_) ++probe_->chunk_calls;
        return {Chunk{d.id() + "#m", d.id(), order, 0, "marker " + d.id(), 2, 0,
                      d.text().size()}};
    }

private:
    std::shared_ptr<Probe> probe_;
};

// throws on a document with id "boom". used to show a strategy failure in
// the middle of rebuild doesn't wipe out the previous corpus
class ThrowingChunker final : public ChunkingStrategy {
public:
    std::vector<Chunk> chunk(const Document& d, std::size_t order) const override {
        if (d.id() == "boom") throw std::runtime_error("chunker failed");
        return Chunker{}.chunk(d, order);
    }
};

// every document gets the same chunk id. passes chunking fine, but
// CorpusIndex rejects duplicate chunk ids - a failure that happens after
// chunking and before rebuild commits
class SameIdChunker final : public ChunkingStrategy {
public:
    std::vector<Chunk> chunk(const Document& d, std::size_t order) const override {
        return {Chunk{"same", d.id(), order, 0, "dup words", 2, 0, d.text().size()}};
    }
};

// ignores the query. returns every chunk in REVERSE corpus order with a
// fixed descending score. the default engine would never produce this
class ReverseRetrieval final : public RetrievalStrategy {
public:
    explicit ReverseRetrieval(std::shared_ptr<Probe> p = nullptr) : probe_(std::move(p)) {}
    ~ReverseRetrieval() override {
        if (probe_) ++probe_->destroyed;
    }
    std::vector<SearchResult> search(const std::string&, int k,
                                     const std::vector<Chunk>& chunks,
                                     const CorpusIndex&) const override {
        if (probe_) {
            ++probe_->search_calls;
            probe_->last_k = k;
        }
        std::vector<SearchResult> out;
        double score = 100.0;
        for (auto it = chunks.rbegin(); it != chunks.rend(); ++it) {
            if (static_cast<int>(out.size()) >= k) break;
            out.push_back(SearchResult{it->id, it->document_id, it->sequence, it->text,
                                       score, 0});
            score -= 1.0;
        }
        return out;
    }

private:
    std::shared_ptr<Probe> probe_;
};

// one item per result, text replaced with "ctx:<chunk id>", never truncates
// and ignores the token budget (not M1 behavior on purpose)
class TagContext final : public ContextStrategy {
public:
    explicit TagContext(std::shared_ptr<Probe> p = nullptr) : probe_(std::move(p)) {}
    ~TagContext() override {
        if (probe_) ++probe_->destroyed;
    }
    std::vector<ContextItem> build(const std::vector<SearchResult>& ranked,
                                   std::size_t budget) const override {
        if (probe_) {
            ++probe_->build_calls;
            probe_->last_budget = budget;
        }
        std::vector<ContextItem> out;
        for (const auto& r : ranked) {
            out.push_back(ContextItem{r.chunk_id, r.document_id, r.chunk_sequence,
                                      "ctx:" + r.chunk_id, 1, r.score, false});
        }
        return out;
    }

private:
    std::shared_ptr<Probe> probe_;
};

Workspace three_docs() {
    Workspace ws;
    ws.add_document(Document{"a", "", "Alpha beta beta."});
    ws.add_document(Document{"b", "", "Gamma alpha."});
    ws.add_document(Document{"c", "", "Delta epsilon."});
    return ws;
}

ProcessingCore probed_core(const std::shared_ptr<Probe>& p) {
    return ProcessingCore(std::make_unique<MarkerChunker>(p),
                          std::make_unique<ReverseRetrieval>(p),
                          std::make_unique<TagContext>(p));
}

// ---- 1. default configuration = M1 ---------------------------------------

void test_default_matches_help_session_trace() {
    // the worked example from the M2 help session, traced end to end
    Workspace ws;
    ws.add_document(Document{"doc1", "", "Search... SEARCH!! 42-times?"});
    ProcessingCore core;
    core.rebuild(ws);

    check(core.chunk_count() == 1, "default: one chunk");
    check(core.chunks()[0].id == "doc1#0", "default: chunk id doc1#0");
    check(core.chunks()[0].text == "search search 42 times", "default: normalized chunk text");
    check(core.chunks()[0].token_count == 4, "default: token_count 4");
    check(core.term_frequency("SEARCH", "doc1#0") == 2, "default: tf(search) = 2");
    check(core.document_frequency("times") == 1, "default: df(times) = 1");

    auto r = core.search("SEARCH times?", 3);
    check(r.size() == 1, "default: one result");
    check(r.size() == 1 && std::fabs(r[0].score - 2.962461898616) < 1e-12,
          "default: TF-IDF * coverage score 2.962461898616");
    check(r.size() == 1 && r[0].matched_terms == 2, "default: both terms matched");

    auto c = core.build_context("SEARCH times?", 3, 3);
    check(c.size() == 1, "default: one context item");
    check(c.size() == 1 && c[0].text == "search search 42", "default: budget truncates text");
    check(c.size() == 1 && c[0].token_count == 3 && c[0].truncated,
          "default: truncated flag + token_count");
}

void test_default_edge_cases() {
    ProcessingCore core;
    core.rebuild(three_docs());

    check(core.search("alpha", 0).empty(), "default: k = 0 -> empty");
    check(throws<std::invalid_argument>([&] { core.search("alpha", -1); }),
          "default: k < 0 throws");
    check(core.search("  ...  ", 5).empty(), "default: effectively empty query -> empty");
    check(core.build_context("alpha", 5, 0).empty(), "default: zero budget -> empty");
    check(throws<std::invalid_argument>([&] { core.build_context("alpha", -1, 5); }),
          "default: build_context k < 0 throws");

    // alpha is in a and b. a = "alpha beta beta" (3 tokens), b = "gamma alpha"
    // same tf/idf for alpha, so tie-break by document order -> a first
    auto r = core.search("alpha", 5);
    check(r.size() == 2 && r[0].document_id == "a" && r[1].document_id == "b",
          "default: deterministic tie-break by document order");

    ProcessingCore empty;
    empty.rebuild(Workspace{});
    check(empty.chunk_count() == 0 && empty.search("alpha", 3).empty(),
          "default: empty workspace -> empty corpus and results");
}

// ---- 2. runtime dispatch --------------------------------------------------

void test_each_operation_dispatches_to_injected_strategy() {
    auto p = std::make_shared<Probe>();
    ProcessingCore core = probed_core(p);

    core.rebuild(three_docs());
    check(p->chunk_calls == 3, "dispatch: chunk() called once per document");
    check(core.chunk_count() == 3 && core.chunks()[0].id == "a#m",
          "dispatch: core stores the custom chunker's output");

    auto r = core.search("alpha", 2);
    check(p->search_calls == 1 && p->last_k == 2, "dispatch: search() reached strategy with k");
    // default engine would return a then b (only docs containing alpha).
    // the reverse strategy returns c then b, which no default path could produce
    check(r.size() == 2 && r[0].chunk_id == "c#m" && r[1].chunk_id == "b#m",
          "dispatch: ranking comes from the injected strategy");

    auto c = core.build_context("alpha", 2, 7);
    check(p->search_calls == 2, "dispatch: build_context gets results from retrieval strategy");
    check(p->build_calls == 1 && p->last_budget == 7, "dispatch: build() reached with budget");
    check(c.size() == 2 && c[0].text == "ctx:c#m" && !c[0].truncated,
          "dispatch: context comes from the injected strategy");
}

void test_mixed_configuration() {
    // only retrieval is custom. chunking and context are the real defaults,
    // so this shows one role can be swapped without touching the others
    ProcessingCore core(std::make_unique<Chunker>(), std::make_unique<ReverseRetrieval>(),
                        std::make_unique<ContextBuilder>());
    core.rebuild(three_docs());
    check(core.chunks()[0].id == "a#0", "mixed: default Chunker ids");

    auto c = core.build_context("whatever", 1, 1);
    // reverse retrieval picks c#0 "delta epsilon", ContextBuilder cuts it to 1 token
    check(c.size() == 1 && c[0].chunk_id == "c#0" && c[0].text == "delta" && c[0].truncated,
          "mixed: custom ranking fed into default ContextBuilder");
}

void test_zero_budget_short_circuits_before_context_strategy() {
    auto p = std::make_shared<Probe>();
    ProcessingCore core = probed_core(p);
    core.rebuild(three_docs());
    check(core.build_context("alpha", 3, 0).empty(), "zero budget: empty result");
    check(p->build_calls == 0 && p->search_calls == 0,
          "zero budget: core handles it, strategies not called");
}

// ---- 3. invalid configuration --------------------------------------------

void test_null_strategies_rejected() {
    auto p = std::make_shared<Probe>();

    check(throws<std::invalid_argument>([&] {
              ProcessingCore core(nullptr, std::make_unique<ReverseRetrieval>(p),
                                  std::make_unique<TagContext>(p));
          }),
          "null chunking rejected");
    check(throws<std::invalid_argument>([&] {
              ProcessingCore core(std::make_unique<MarkerChunker>(p), nullptr,
                                  std::make_unique<TagContext>(p));
          }),
          "null retrieval rejected");
    check(throws<std::invalid_argument>([&] {
              ProcessingCore core(std::make_unique<MarkerChunker>(p),
                                  std::make_unique<ReverseRetrieval>(p), nullptr);
          }),
          "null context rejected");
    check(throws<std::invalid_argument>([&] { ProcessingCore core(nullptr, nullptr, nullptr); }),
          "all null rejected");

    // 6 non-null strategies were handed over across the three failing
    // constructions. each must be freed exactly once even though the
    // constructor threw
    check(p->destroyed == 6, "null config: valid strategies still freed exactly once");
}

// ---- 4. ownership and lifetime -------------------------------------------

void test_core_owns_and_destroys_strategies_once() {
    auto p = std::make_shared<Probe>();
    {
        auto chunking = std::make_unique<MarkerChunker>(p);
        ProcessingCore core(std::move(chunking), std::make_unique<ReverseRetrieval>(p),
                            std::make_unique<TagContext>(p));
        check(chunking == nullptr, "ownership: caller's unique_ptr is empty after transfer");
        check(p->destroyed == 0, "ownership: strategies alive while core alive");
    }
    // destroyed through unique_ptr<Base>. the derived destructor ran, which
    // only happens because the base destructor is virtual
    check(p->destroyed == 3, "ownership: all three destroyed once with the core");
}

void test_move_construct_transfers_strategies() {
    auto p = std::make_shared<Probe>();
    {
        ProcessingCore a = probed_core(p);
        a.rebuild(three_docs());

        ProcessingCore b(std::move(a));
        check(p->destroyed == 0, "move ctor: nothing destroyed by the move");
        check(b.chunk_count() == 3, "move ctor: corpus moved with core");
        auto r = b.search("x", 1);
        check(r.size() == 1 && r[0].chunk_id == "c#m", "move ctor: same injected strategy used");

        // moved-from core: safe empty reads, rebuild refuses
        check(a.chunk_count() == 0 && a.chunks().empty(), "moved-from: empty corpus");
        check(a.search("alpha", 3).empty(), "moved-from: search empty");
        check(a.build_context("alpha", 3, 5).empty(), "moved-from: context empty");
        check(throws<std::logic_error>([&] { a.rebuild(three_docs()); }),
              "moved-from: rebuild throws instead of silently using defaults");
    }
    check(p->destroyed == 3, "move ctor: strategies destroyed once, not twice");
}

void test_move_assign_releases_old_strategies() {
    auto old_p = std::make_shared<Probe>();
    auto new_p = std::make_shared<Probe>();
    {
        ProcessingCore target = probed_core(old_p);
        ProcessingCore source = probed_core(new_p);
        source.rebuild(three_docs());

        target = std::move(source);
        check(old_p->destroyed == 3, "move assign: target's old strategies released");
        check(new_p->destroyed == 0, "move assign: moved strategies still alive");

        target.search("x", 1);
        check(new_p->search_calls == 1 && old_p->search_calls == 0,
              "move assign: target now dispatches to the moved-in strategy");

        // a moved-from core can be reused by assigning a new core into it
        source = ProcessingCore{};
        source.rebuild(three_docs());
        check(source.chunks()[0].id == "a#0", "move assign: moved-from core reusable");
    }
    check(new_p->destroyed == 3, "move assign: moved strategies destroyed once at end");
}

// ---- 5. rebuild across responsibilities ----------------------------------

void test_custom_chunking_changes_what_default_retrieval_finds() {
    // custom chunking + default retrieval and context. the words in the
    // documents never reach the index, only "marker <id>" does
    ProcessingCore core(std::make_unique<MarkerChunker>(), std::make_unique<RetrievalEngine>(),
                        std::make_unique<ContextBuilder>());
    core.rebuild(three_docs());

    check(core.search("alpha", 5).empty(), "cross: original words not indexed");
    check(core.document_frequency("marker") == 3, "cross: index built from custom chunks");

    auto r = core.search("marker b", 5);
    check(!r.empty() && r[0].chunk_id == "b#m", "cross: default engine ranks custom chunks");

    auto c = core.build_context("marker", 5, 4);
    // three 2-token chunks, budget 4 -> a#m whole, b#m whole, stop
    check(c.size() == 2 && c[0].text == "marker a" && c[1].text == "marker b",
          "cross: default context budget over custom chunks");
}

void test_failed_rebuild_keeps_corpus_and_strategies() {
    auto p = std::make_shared<Probe>();
    ProcessingCore core = probed_core(p);
    core.rebuild(three_docs());
    const auto before = core.chunks();

    Workspace dup;
    dup.add_document(Document{"x", "", "one"});
    dup.add_document(Document{"x", "", "two"});
    check(throws<std::invalid_argument>([&] { core.rebuild(dup); }), "rollback: duplicate id throws");

    check(core.chunk_count() == before.size() && core.chunks()[0].id == before[0].id,
          "rollback: previous corpus unchanged");
    check(core.document_frequency("marker") == 3, "rollback: previous index unchanged");
    auto r = core.search("x", 1);
    check(r.size() == 1 && r[0].chunk_id == "c#m", "rollback: same injected strategy still used");
    check(p->destroyed == 0, "rollback: strategies not destroyed by failed rebuild");

    // rebuild replaces, never merges
    Workspace one;
    one.add_document(Document{"z", "", "zeta"});
    core.rebuild(one);
    check(core.chunk_count() == 1 && core.chunks()[0].id == "z#m", "rebuild: replaces old corpus");
}

void test_strategy_exception_mid_rebuild_keeps_corpus() {
    ProcessingCore core(std::make_unique<ThrowingChunker>(), std::make_unique<RetrievalEngine>(),
                        std::make_unique<ContextBuilder>());
    core.rebuild(three_docs());

    Workspace bad;
    bad.add_document(Document{"ok", "", "fresh words"});
    bad.add_document(Document{"boom", "", "never chunked"});
    check(throws<std::runtime_error>([&] { core.rebuild(bad); }),
          "strategy throw: exception propagates");
    check(core.chunk_count() == 3 && core.document_frequency("fresh") == 0,
          "strategy throw: half-built corpus never committed");
    check(!core.search("alpha", 3).empty(), "strategy throw: old corpus still searchable");
}

void test_index_failure_after_chunking_keeps_corpus() {
    // with one document the duplicate-id chunker is still valid, so this
    // gives a real corpus to protect
    ProcessingCore core(std::make_unique<SameIdChunker>(), std::make_unique<RetrievalEngine>(),
                        std::make_unique<ContextBuilder>());
    Workspace one;
    one.add_document(Document{"a", "", "alpha"});
    core.rebuild(one);  // one chunk "same", valid
    check(core.chunk_count() == 1, "index failure: initial corpus built");

    Workspace two;
    two.add_document(Document{"a", "", "alpha"});
    two.add_document(Document{"b", "", "beta"});
    check(throws<std::invalid_argument>([&] { core.rebuild(two); }),
          "index failure: duplicate chunk ids rejected by CorpusIndex");
    check(core.chunk_count() == 1 && core.document_frequency("dup") == 1,
          "index failure: chunks and index both unchanged (no early commit)");
}


// ---- M4 / CR-1 helpers ---------------------------------------------------

using aiws::gui::MainWindow;

// fails every build. the factory runs on the worker thread, so the throw
// happens exactly where a real chunking failure would
class FailingChunker final : public ChunkingStrategy {
public:
    std::vector<Chunk> chunk(const Document&, std::size_t) const override {
        throw std::runtime_error("chunking failed on purpose");
    }
};

// matches CorpusBuildJob::CoreFactory, ProcessingCore (*)()
ProcessingCore failing_core() {
    return ProcessingCore(std::make_unique<FailingChunker>(), std::make_unique<RetrievalEngine>(),
                          std::make_unique<ContextBuilder>());
}

// tests make their own documents in a temp dir, so they don't depend on the
// directory ctest happens to run in
QString write_file(const QTemporaryDir& dir, const QString& name, const QByteArray& text) {
    const QString path = dir.path() + "/" + name;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.write(text);
    return path;
}

QPushButton* find_button(MainWindow& w, const QString& text) {
    for (QPushButton* b : w.findChildren<QPushButton*>()) {
        if (b->text() == text) return b;
    }
    return nullptr;
}

void click(MainWindow& w, const QString& text) {
    QPushButton* b = find_button(w, text);
    check(b != nullptr, "button exists: " + text.toStdString());
    if (b) b->click();
}

// spins the event loop (so the queued build_finished / remote add can run)
// until the build is done. false = still building after ms
bool wait_until_idle(MainWindow& w, int ms = 5000) {
    return QTest::qWaitFor([&] { return !w.is_building(); }, ms);
}

bool wait_for_documents(MainWindow& w, std::size_t n, int ms = 3000) {
    return QTest::qWaitFor([&] { return w.document_count() == n; }, ms);
}

// runs the real aiws_send from cwd and returns its exit code (-1 = didn't run)
int run_aiws_send(const QStringList& args, const QString& cwd) {
    QProcess p;
    p.setWorkingDirectory(cwd);
    p.start(AIWS_SEND_PATH, args);
    if (!p.waitForFinished(10000) || p.exitStatus() != QProcess::NormalExit) return -1;
    return p.exitCode();
}

// ---- 6. R1 / R2 ----------------------------------------------------------

void test_build_runs_in_background() {
    QTemporaryDir dir;
    MainWindow w;
    check(w.add_document_file(write_file(dir, "a.txt", "alpha beta gamma")), "R1: add a document");
    w.set_build_delay_ms(400);

    QElapsedTimer t;
    t.start();
    click(w, "Build Corpus");
    check(t.elapsed() < 200, "R1: Build Corpus returns right away even with a 400 ms build");
    check(w.is_building(), "R1: build is running after the click");
    check(!find_button(w, "Build Corpus")->isEnabled(), "R1: one build at a time (Build disabled)");

    check(wait_until_idle(w), "R1: build finishes");
    check(w.is_corpus_current(), "R2: build of an unchanged workspace becomes current");
}

// the core R2 case (and what a stale-accepting build gets wrong): the build
// started for one document, a second one is added before it finishes
void test_add_during_build_discards_result() {
    QTemporaryDir dir;
    MainWindow w;
    w.add_document_file(write_file(dir, "a.txt", "alpha beta"));
    w.set_build_delay_ms(400);
    click(w, "Build Corpus");

    check(w.add_document_file(write_file(dir, "b.txt", "delta epsilon")), "R2: Add Document works during a build");
    check(wait_until_idle(w), "R2: stale build still finishes");
    check(!w.is_corpus_current(), "R2: build from before the add is NOT current");
    check(find_button(w, "Build Corpus")->isEnabled(), "R2: can build again afterwards");

    w.set_build_delay_ms(0);
    click(w, "Build Corpus");
    check(wait_until_idle(w) && w.is_corpus_current(), "R2: rebuilding the new workspace makes it current");
}

// same doc count before and after, so only a generation check catches it
void test_reset_and_readd_during_build_discards_result() {
    QTemporaryDir dir;
    const QString a = write_file(dir, "a.txt", "alpha beta");
    MainWindow w;
    w.add_document_file(a);
    w.set_build_delay_ms(400);
    click(w, "Build Corpus");

    click(w, "Reset");
    w.add_document_file(a);
    check(wait_until_idle(w), "R2: build finishes after Reset");
    check(!w.is_corpus_current(), "R2: Reset + re-adding the same file still discards the old build");
}

void test_unreadable_add_changes_nothing() {
    QTemporaryDir dir;
    MainWindow w;
    w.add_document_file(write_file(dir, "a.txt", "alpha"));
    click(w, "Build Corpus");
    wait_until_idle(w);

    check(!w.add_document_file(dir.path() + "/missing.txt"), "add_document_file: false for an unreadable file");
    check(w.document_count() == 1 && w.is_corpus_current(), "add_document_file: unreadable file changes nothing");
}

// ---- 7. R3 / R4 ----------------------------------------------------------

void test_failed_build_is_reported() {
    QTemporaryDir dir;
    MainWindow w;
    w.add_document_file(write_file(dir, "a.txt", "alpha"));
    w.set_core_factory(&failing_core);
    click(w, "Build Corpus");

    check(wait_until_idle(w, 3000), "R3: a failed build leaves the building state");
    check(!w.is_corpus_current(), "R3: failed build is not current");
    check(find_button(w, "Build Corpus")->isEnabled(), "R3: Build is enabled again after a failure");

    w.set_core_factory(nullptr);
    click(w, "Build Corpus");
    check(wait_until_idle(w) && w.is_corpus_current(), "R3: app still usable, next build works");
}

void test_exit_during_long_build_is_quick() {
    QTemporaryDir dir;
    QElapsedTimer t;
    {
        MainWindow w;
        w.add_document_file(write_file(dir, "a.txt", "alpha"));
        w.add_document_file(write_file(dir, "b.txt", "beta"));
        w.set_build_delay_ms(10000);  // a 20 s build
        click(w, "Build Corpus");
        QTest::qWait(50);
        t.start();
    }  // ~MainWindow here, mid-build
    check(t.elapsed() < 1000, "R4: destroying the window mid-build doesn't wait for the build");
}

void test_repeated_builds() {
    QTemporaryDir dir;
    MainWindow w;
    w.add_document_file(write_file(dir, "a.txt", "alpha beta gamma"));
    bool all_ok = true;
    for (int i = 0; i < 20; ++i) {
        click(w, "Build Corpus");
        all_ok = all_ok && wait_until_idle(w, 3000) && w.is_corpus_current();
    }
    check(all_ok, "R4: 20 builds in a row each finish and become current");
}

// ---- 8. CR-1 -------------------------------------------------------------

// E1 + relative path: aiws_send runs in dir, the app doesn't
void test_remote_add_relative_path() {
    QTemporaryDir dir;
    write_file(dir, "notes.txt", "remote alpha");
    const std::string sock = dir.path().toStdString() + "/s.sock";
    MainWindow w;
    check(w.start_remote_add(sock), "E1: server starts");

    check(run_aiws_send({"--socket", QString::fromStdString(sock), "add", "notes.txt"}, dir.path()) == 0,
          "E1: aiws_send exits 0 for a readable relative path");
    check(wait_for_documents(w, 1), "E1: document added as if by Add Document");

    click(w, "Build Corpus");
    check(wait_until_idle(w) && w.is_corpus_current(), "E1: remotely added document builds");
}

// E1 + R2: a remote add during a build has the same effect as a local one
void test_remote_add_during_build_discards_result() {
    QTemporaryDir dir;
    write_file(dir, "b.txt", "beta");
    const std::string sock = dir.path().toStdString() + "/s.sock";
    MainWindow w;
    w.start_remote_add(sock);
    w.add_document_file(write_file(dir, "a.txt", "alpha"));
    w.set_build_delay_ms(400);
    click(w, "Build Corpus");

    check(run_aiws_send({"--socket", QString::fromStdString(sock), "add", "b.txt"}, dir.path()) == 0,
          "E1/R2: remote add accepted during a build");
    check(wait_for_documents(w, 2), "E1/R2: remote document added during the build");
    check(wait_until_idle(w) && !w.is_corpus_current(), "E1/R2: build from before the remote add is NOT current");
}

void test_remote_add_unusual_paths() {
    QTemporaryDir dir;
    const std::string sock = dir.path().toStdString() + "/s.sock";
    MainWindow w;
    w.start_remote_add(sock);

    const QString odd = QString::fromUtf8("my notes caf\xC3\xA9\nline two.txt");  // space, UTF-8, newline
    write_file(dir, odd, "odd path words");
    check(run_aiws_send({"--socket", QString::fromStdString(sock), "add", odd}, dir.path()) == 0,
          "E2: path with a space, UTF-8 and a newline accepted");
    check(wait_for_documents(w, 1), "E2: that document was added");
}

void test_remote_add_rejections() {
    QTemporaryDir dir;
    const QString sock = dir.path() + "/s.sock";
    MainWindow w;
    w.start_remote_add(sock.toStdString());

    check(run_aiws_send({"--socket", sock, "add", "missing.txt"}, dir.path()) == 1, "E1: missing file -> exit 1");
    check(run_aiws_send({"--socket", sock, "add", "."}, dir.path()) == 1, "E1: directory -> exit 1");
    QTest::qWait(100);
    check(w.document_count() == 0, "E1: rejected requests add nothing");

    check(run_aiws_send({"--socket", dir.path() + "/nobody.sock", "add", "x.txt"}, dir.path()) == 2,
          "exit 2: no app on that socket");
}

// E3: a hand-written client sends the request in three pieces with pauses.
// sleeping here is fine: the server answers on its own thread
void test_request_in_pieces() {
    QTemporaryDir dir;
    const std::string sock = dir.path().toStdString() + "/s.sock";
    const std::string path = write_file(dir, "piece.txt", "split request").toStdString();
    MainWindow w;
    w.start_remote_add(sock);

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, sock.c_str(), sizeof(addr.sun_path) - 1);
    const bool connected = ::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0;
    check(connected, "E3: raw client connects");

    const std::string request = ipc::encode(ipc::kCommandAdd, path);
    const std::size_t cuts[] = {0, 2, 7, request.size()};  // mid-header, mid-path, rest
    for (int i = 0; i < 3 && connected; ++i) {
        ::write(fd, request.data() + cuts[i], cuts[i + 1] - cuts[i]);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    unsigned char header[ipc::kHeaderBytes] = {0xFF};
    const auto deadline = ipc::Clock::now() + std::chrono::seconds(3);
    const bool replied = connected && ipc::read_exact(fd, header, sizeof(header), deadline);
    ::close(fd);
    check(replied && header[0] == ipc::kStatusAccepted, "E3: split request accepted");
    check(wait_for_documents(w, 1), "E3: split request added the document");
}

// E4: the window going away (what happens after Exit is confirmed) stops the
// server quickly and removes its socket file
void test_exit_stops_server() {
    QTemporaryDir dir;
    const QString sock = dir.path() + "/s.sock";
    QElapsedTimer t;
    {
        MainWindow w;
        w.start_remote_add(sock.toStdString());
        check(QFile::exists(sock), "E4: socket file exists while running");
        t.start();
    }
    check(t.elapsed() < 500, "E4: stops serving within 500 ms");
    check(!QFile::exists(sock), "E4: socket file removed");
    check(run_aiws_send({"--socket", sock, "add", "x.txt"}, dir.path()) == 2, "E4: nothing answers afterwards");
}

}  // namespace

// QApplication: the M4/CR-1 tests create MainWindows and need an event loop
// for the queued build_finished / remote add calls. offscreen comes from ctest
int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    test_default_matches_help_session_trace();
    test_default_edge_cases();
    test_each_operation_dispatches_to_injected_strategy();
    test_mixed_configuration();
    test_zero_budget_short_circuits_before_context_strategy();
    test_null_strategies_rejected();
    test_core_owns_and_destroys_strategies_once();
    test_move_construct_transfers_strategies();
    test_move_assign_releases_old_strategies();
    test_custom_chunking_changes_what_default_retrieval_finds();
    test_failed_rebuild_keeps_corpus_and_strategies();
    test_strategy_exception_mid_rebuild_keeps_corpus();
    test_index_failure_after_chunking_keeps_corpus();

    test_build_runs_in_background();
    test_add_during_build_discards_result();
    test_reset_and_readd_during_build_discards_result();
    test_unreadable_add_changes_nothing();
    test_failed_build_is_reported();
    test_exit_during_long_build_is_quick();
    test_repeated_builds();

    test_remote_add_relative_path();
    test_remote_add_during_build_discards_result();
    test_remote_add_unusual_paths();
    test_remote_add_rejections();
    test_request_in_pieces();
    test_exit_stops_server();

    if (failures) {
        std::cerr << failures << " of " << checks << " checks failed\n";
        return 1;
    }
    std::cout << "student tests passed (" << checks << " checks)\n";
    return 0;
}
