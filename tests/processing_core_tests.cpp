#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "aiws/processing_core.hpp"
#include "aiws/text_processor.hpp"

using aiws::Chunk;
using aiws::ContextItem;
using aiws::Document;
using aiws::ProcessingCore;
using aiws::SearchResult;
using aiws::TextProcessor;
using aiws::Workspace;

static std::string words(const std::string& prefix, int from, int to) {
    std::string s;
    for (int i = from; i < to; ++i) {
        if (!s.empty()) {
            s += ' ';
        }
        s += prefix + std::to_string(i);
    }
    return s;
}

template <typename F>
static bool throws_invalid(F f) {
    try {
        f();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

// three real documents, one long one with a paragraph break in the window,
// plus a punctuation only one that should make no chunks
static Workspace sample_workspace() {
    Workspace ws;
    ws.add_document(Document{"guide", "Guide",
        "Retrieval systems build an INDEX.\r\n\r\nThe index maps each term to chunks."});
    ws.add_document(Document{"long", "Long",
        words("x", 0, 110) + " retrieval\n\n" + words("x", 110, 250) + " index index"});
    ws.add_document(Document{"blank", "Blank", "... --- !!!"});
    ws.add_document(Document{"notes", "Notes", "Unrelated notes about cooking pasta."});
    return ws;
}

static void empty_core_test() {
    ProcessingCore core;
    assert(core.chunk_count() == 0);
    assert(core.chunks().empty());
    assert(core.search("anything", 5).empty());
    assert(core.build_context("anything", 5, 100).empty());
    assert(core.document_frequency("anything") == 0);

    // rebuilding from an empty workspace is valid
    core.rebuild(Workspace{});
    assert(core.chunk_count() == 0);
}

static void normalize_matches_text_processor_test() {
    const char* inputs[] = {"Hello,  WORLD! 2026", "", "R2-D2", "a\r\n\r\nb", "caf\xC3\xA9"};
    for (const char* in : inputs) {
        assert(ProcessingCore::normalize(in) == TextProcessor::normalize(in));
    }
}

static void multi_document_chunks_test() {
    Workspace ws = sample_workspace();
    ProcessingCore core;
    core.rebuild(ws);

    const std::vector<Chunk>& chunks = core.chunks();
    // guide 1, long 3 (break before token 111, then [91,211) [191,253)), blank 0, notes 1
    assert(core.chunk_count() == 5);
    assert(chunks[0].id == "guide#0");
    assert(chunks[1].id == "long#0" && chunks[1].token_count == 111);
    assert(chunks[2].id == "long#1");
    assert(chunks[3].id == "long#2");
    assert(chunks[4].id == "notes#0");

    // document order skips nothing even though blank made no chunks
    assert(chunks[0].document_order == 0);
    assert(chunks[1].document_order == 1);
    assert(chunks[4].document_order == 3);

    // every span maps back into its own document
    for (const Chunk& c : chunks) {
        const Document* source = nullptr;
        for (const Document& d : ws.documents()) {
            if (d.id() == c.document_id) {
                source = &d;
            }
        }
        assert(source != nullptr);
        std::string original =
            source->text().substr(c.source_begin, c.source_end - c.source_begin);
        assert(TextProcessor::normalize(original) == c.text);
    }
}

static void term_argument_test() {
    ProcessingCore core;
    core.rebuild(sample_workspace());

    // arguments get normalized, empty ones give 0, multi token ones throw
    assert(core.document_frequency("INDEX!") == core.document_frequency("index"));
    assert(core.document_frequency("") == 0);
    assert(core.document_frequency("?!") == 0);
    assert(core.term_frequency("...", "guide#0") == 0);
    assert(throws_invalid([&] { (void)core.document_frequency("two words"); }));
    assert(throws_invalid([&] { (void)core.term_frequency("r2-d2", "guide#0"); }));

    // guide#0 has index twice, long#2 ends with index index
    assert(core.term_frequency("index", "guide#0") == 2);
    assert(core.term_frequency("index", "long#2") == 2);
    assert(core.term_frequency("index", "missing#9") == 0);
    assert(core.document_frequency("index") == 2);
    assert(core.document_frequency("pasta") == 1);
}

static void search_across_documents_test() {
    ProcessingCore core;
    core.rebuild(sample_workspace());

    std::vector<SearchResult> r = core.search("retrieval index", 10);
    // guide#0 has both. retrieval is token 110 of long, which is in long#0
    // and also in the overlap at the start of long#1. long#2 has index twice
    assert(r.size() == 4);
    assert(r[0].chunk_id == "guide#0" && r[0].matched_terms == 2);
    for (const SearchResult& x : r) {
        assert(x.document_id != "notes");
    }
    for (std::size_t i = 1; i < r.size(); ++i) {
        assert(r[i - 1].score >= r[i].score);
    }

    assert(core.search("pasta", 10).size() == 1);
    assert(core.search("unknownword", 10).empty());
    assert(core.search("retrieval", 0).empty());
    assert(throws_invalid([&] { (void)core.search("retrieval", -1); }));
}

static void context_test() {
    ProcessingCore core;
    core.rebuild(sample_workspace());

    std::vector<SearchResult> ranked = core.search("retrieval index", 10);
    std::vector<ContextItem> all = core.build_context("retrieval index", 10, 100000);

    // same order as search, nothing reranked
    assert(all.size() == ranked.size());
    for (std::size_t i = 0; i < all.size(); ++i) {
        assert(all[i].chunk_id == ranked[i].chunk_id);
        assert(all[i].score == ranked[i].score);
        assert(!all[i].truncated);
    }

    // budget ends inside the second chunk
    std::size_t first = all[0].token_count;
    std::vector<ContextItem> cut = core.build_context("retrieval index", 10, first + 5);
    assert(cut.size() == 2);
    assert(cut[1].truncated && cut[1].token_count == 5);
    assert(cut[0].token_count + cut[1].token_count == first + 5);

    assert(core.build_context("retrieval index", 10, 0).empty());
    assert(core.build_context("retrieval index", 0, 100).empty());
    assert(throws_invalid([&] { (void)core.build_context("retrieval", -2, 10); }));
}

static void rebuild_replaces_test() {
    ProcessingCore core;
    core.rebuild(sample_workspace());
    assert(core.document_frequency("pasta") == 1);

    // drop notes and guide, nothing of theirs can be left over
    Workspace smaller;
    smaller.add_document(Document{"long", "Long", words("x", 0, 50)});
    core.rebuild(smaller);
    assert(core.chunk_count() == 1);
    assert(core.document_frequency("pasta") == 0);
    assert(core.term_frequency("index", "guide#0") == 0);
    assert(core.search("pasta retrieval index", 10).empty());

    // repeated rebuilds give the same state, not doubled state
    core.rebuild(smaller);
    core.rebuild(smaller);
    assert(core.chunk_count() == 1);
    assert(core.document_frequency("x10") == 1);

    // rebuild does not change the documents it reads
    Workspace ws = sample_workspace();
    std::string before = ws.documents()[0].text();
    core.rebuild(ws);
    assert(ws.documents()[0].text() == before);
}

static void failed_rebuild_keeps_old_corpus_test() {
    ProcessingCore core;
    core.rebuild(sample_workspace());
    std::vector<Chunk> before = core.chunks();
    std::vector<SearchResult> search_before = core.search("retrieval index", 10);

    Workspace dup;
    dup.add_document(Document{"a", "A", "brand new words"});
    dup.add_document(Document{"a", "A again", "more new words"});
    assert(throws_invalid([&] { core.rebuild(dup); }));

    // a duplicate that has no text makes no chunks but is still invalid
    Workspace dup_empty;
    dup_empty.add_document(Document{"a", "A", "brand new words"});
    dup_empty.add_document(Document{"a", "A again", "..."});
    assert(throws_invalid([&] { core.rebuild(dup_empty); }));

    // exactly the old corpus, none of the new words
    assert(core.chunks().size() == before.size());
    for (std::size_t i = 0; i < before.size(); ++i) {
        assert(core.chunks()[i].id == before[i].id);
        assert(core.chunks()[i].text == before[i].text);
    }
    assert(core.document_frequency("brand") == 0);
    std::vector<SearchResult> search_after = core.search("retrieval index", 10);
    assert(search_after.size() == search_before.size());
    for (std::size_t i = 0; i < search_after.size(); ++i) {
        assert(search_after[i].chunk_id == search_before[i].chunk_id);
        assert(search_after[i].score == search_before[i].score);
    }
}

static void move_test() {
    ProcessingCore core;
    core.rebuild(sample_workspace());
    std::size_t count = core.chunk_count();

    ProcessingCore moved(std::move(core));
    assert(moved.chunk_count() == count);
    assert(!moved.search("retrieval", 5).empty());

    // moved-from core acts empty and can be rebuilt
    assert(core.chunk_count() == 0);
    assert(core.search("retrieval", 5).empty());
    core.rebuild(sample_workspace());
    assert(core.chunk_count() == count);
}

int main() {
    empty_core_test();
    normalize_matches_text_processor_test();
    multi_document_chunks_test();
    term_argument_test();
    search_across_documents_test();
    context_test();
    rebuild_replaces_test();
    failed_rebuild_keeps_old_corpus_test();
    move_test();
    std::cout << "processing_core tests passed\n";
    return 0;
}