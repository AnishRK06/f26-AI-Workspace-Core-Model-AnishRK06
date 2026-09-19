#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "aiws/chunker.hpp"
#include "aiws/corpus_index.hpp"
#include "aiws/document.hpp"

using aiws::Chunk;
using aiws::Chunker;
using aiws::CorpusIndex;
using aiws::Document;

// just the fields the index cares about, text is already normalized
static Chunk make_chunk(const std::string& id, const std::string& text) {
    Chunk c;
    c.id = id;
    c.text = text;
    return c;
}

static std::vector<Chunk> sample() {
    return {
        make_chunk("a#0", "alpha alpha beta"),
        make_chunk("a#1", "alpha gamma"),
        make_chunk("b#0", "delta"),
    };
}

static void empty_index_test() {
    CorpusIndex index;
    assert(index.document_frequency("alpha") == 0);
    assert(index.term_frequency("alpha", "a#0") == 0);
    assert(index.postings("alpha") == nullptr);
    assert(index.find_chunk({}, "a#0") == nullptr);

    bool threw = false;
    try {
        (void)index.chunk_index("a#0");
    } catch (const std::out_of_range&) {
        threw = true;
    }
    assert(threw);
}

static void frequency_test() {
    std::vector<Chunk> chunks = sample();
    CorpusIndex index(chunks);

    // df counts chunks, not occurrences
    assert(index.document_frequency("alpha") == 2);
    assert(index.document_frequency("beta") == 1);
    assert(index.document_frequency("delta") == 1);
    assert(index.document_frequency("zeta") == 0);
    assert(index.document_frequency("") == 0);

    assert(index.term_frequency("alpha", "a#0") == 2);
    assert(index.term_frequency("alpha", "a#1") == 1);
    assert(index.term_frequency("alpha", "b#0") == 0);
    assert(index.term_frequency("alpha", "nope#0") == 0);
    assert(index.term_frequency("zeta", "a#0") == 0);
}

static void postings_test() {
    std::vector<Chunk> chunks = sample();
    CorpusIndex index(chunks);

    const std::vector<CorpusIndex::Posting>* alpha = index.postings("alpha");
    assert(alpha != nullptr);
    assert(alpha->size() == 2);
    // one posting per chunk, in chunk order
    assert((*alpha)[0].chunk_index == 0 && (*alpha)[0].frequency == 2);
    assert((*alpha)[1].chunk_index == 1 && (*alpha)[1].frequency == 1);

    // every token in the corpus shows up in exactly one posting frequency
    std::size_t total = 0;
    for (const char* term : {"alpha", "beta", "gamma", "delta"}) {
        for (const CorpusIndex::Posting& p : *index.postings(term)) {
            total += p.frequency;
        }
    }
    assert(total == 6);
}

static void lookup_test() {
    std::vector<Chunk> chunks = sample();
    CorpusIndex index(chunks);

    assert(index.chunk_index("a#0") == 0);
    assert(index.chunk_index("b#0") == 2);

    const Chunk* found = index.find_chunk(chunks, "a#1");
    assert(found == &chunks[1]);
    assert(index.find_chunk(chunks, "missing") == nullptr);

    // a vector the index wasnt built from gives nullptr, not the wrong chunk
    std::vector<Chunk> other = {make_chunk("x#0", "x")};
    assert(index.find_chunk(other, "a#0") == nullptr);
}

static void rebuild_replaces_test() {
    std::vector<Chunk> first = sample();
    CorpusIndex index(first);

    std::vector<Chunk> second = {make_chunk("c#0", "gamma omega")};
    index.build(second);

    // nothing from the first build is left behind
    assert(index.document_frequency("alpha") == 0);
    assert(index.postings("beta") == nullptr);
    assert(index.term_frequency("alpha", "a#0") == 0);
    assert(index.find_chunk(first, "a#0") == nullptr);
    assert(index.document_frequency("gamma") == 1);
    assert(index.chunk_index("c#0") == 0);

    // building the same chunks twice doesnt pile up postings
    index.build(second);
    index.build(second);
    assert(index.document_frequency("gamma") == 1);
    assert(index.postings("omega")->size() == 1);

    // building from nothing empties it
    index.build({});
    assert(index.document_frequency("gamma") == 0);
}

static void failed_build_test() {
    std::vector<Chunk> good = sample();
    CorpusIndex index(good);

    std::vector<Chunk> bad = {
        make_chunk("z#0", "zeta"),
        make_chunk("z#0", "zeta again"),
    };
    bool threw = false;
    try {
        index.build(bad);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);

    // old index is intact and the bad data never got in
    assert(index.document_frequency("alpha") == 2);
    assert(index.term_frequency("alpha", "a#0") == 2);
    assert(index.document_frequency("zeta") == 0);
    assert(index.chunk_index("b#0") == 2);
}

static void with_chunker_test() {
    // 130 tokens gives [0,120) and [100,130), w100..w119 are in both
    std::string text;
    for (int i = 0; i < 130; ++i) {
        text += "w" + std::to_string(i) + " ";
    }
    Chunker chunker;
    std::vector<Chunk> chunks = chunker.chunk(Document{"d", "D", text}, 0);
    CorpusIndex index(chunks);

    assert(index.document_frequency("w0") == 1);
    assert(index.document_frequency("w110") == 2);
    assert(index.document_frequency("w125") == 1);
    assert(index.term_frequency("w110", "d#1") == 1);
    assert(index.term_frequency("w0", "d#1") == 0);
}

int main() {
    empty_index_test();
    frequency_test();
    postings_test();
    lookup_test();
    rebuild_replaces_test();
    failed_build_test();
    with_chunker_test();
    std::cout << "corpus_index tests passed\n";
    return 0;
}