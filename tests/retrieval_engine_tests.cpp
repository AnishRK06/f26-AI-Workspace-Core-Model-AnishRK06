#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "aiws/corpus_index.hpp"
#include "aiws/retrieval_engine.hpp"

using aiws::Chunk;
using aiws::CorpusIndex;
using aiws::RetrievalEngine;
using aiws::SearchResult;

static Chunk make_chunk(const std::string& doc, std::size_t order,
                        std::size_t seq, const std::string& text) {
    Chunk c;
    c.id = doc + "#" + std::to_string(seq);
    c.document_id = doc;
    c.document_order = order;
    c.sequence = seq;
    c.text = text;
    return c;
}

static std::vector<Chunk> sample() {
    return {
        make_chunk("a", 0, 0, "alpha alpha beta"),
        make_chunk("a", 0, 1, "alpha gamma"),
        make_chunk("b", 1, 0, "delta"),
    };
}

static double round12(double v) {
    return std::round(v * 1e12) / 1e12;
}

static bool same(double a, double b) {
    return std::fabs(a - b) < 1e-12;
}

static void empty_query_test() {
    std::vector<Chunk> chunks = sample();
    CorpusIndex index(chunks);
    RetrievalEngine engine;

    assert(engine.search("", 5, chunks, index).empty());
    assert(engine.search("?!...", 5, chunks, index).empty());
    assert(engine.search("zzz yyy", 5, chunks, index).empty());
    assert(engine.search("alpha", 0, chunks, index).empty());

    // no chunks at all is fine too
    CorpusIndex empty;
    assert(engine.search("alpha", 5, {}, empty).empty());
}

static void negative_k_test() {
    std::vector<Chunk> chunks = sample();
    CorpusIndex index(chunks);
    RetrievalEngine engine;

    bool threw = false;
    try {
        (void)engine.search("alpha", -1, chunks, index);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);

    // throws even when the query is empty
    threw = false;
    try {
        (void)engine.search("", -3, chunks, index);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);
}

static void single_term_score_test() {
    std::vector<Chunk> chunks = sample();
    CorpusIndex index(chunks);
    RetrievalEngine engine;

    std::vector<SearchResult> r = engine.search("Alpha!", 10, chunks, index);

    // only chunks with alpha come back, delta chunk is not a candidate
    assert(r.size() == 2);

    // N = 3, DF(alpha) = 2, one query term so coverage = 1.1
    double idf = std::log(4.0 / 3.0) + 1.0;
    double a0 = round12((1.0 + std::log(2.0)) * idf * 1.1);
    double a1 = round12(1.0 * idf * 1.1);

    assert(r[0].chunk_id == "a#0" && same(r[0].score, a0));
    assert(r[1].chunk_id == "a#1" && same(r[1].score, a1));
    assert(r[0].matched_terms == 1);
    assert(r[0].document_id == "a");
    assert(r[0].chunk_sequence == 0);
    assert(r[0].text == "alpha alpha beta");
}

static void coverage_test() {
    std::vector<Chunk> chunks = sample();
    CorpusIndex index(chunks);
    RetrievalEngine engine;

    std::vector<SearchResult> r = engine.search("alpha beta", 10, chunks, index);
    assert(r.size() == 2);

    double idf_alpha = std::log(4.0 / 3.0) + 1.0;
    double idf_beta = std::log(4.0 / 2.0) + 1.0;
    // a#0 matches both terms, a#1 only alpha
    double a0 = round12(((1.0 + std::log(2.0)) * idf_alpha + idf_beta) * (1.0 + 0.10 * 2 / 2));
    double a1 = round12(idf_alpha * (1.0 + 0.10 * 1 / 2));

    assert(same(r[0].score, a0) && r[0].matched_terms == 2);
    assert(same(r[1].score, a1) && r[1].matched_terms == 1);
}

static void unknown_term_counts_in_q_test() {
    std::vector<Chunk> chunks = sample();
    CorpusIndex index(chunks);
    RetrievalEngine engine;

    // zzz adds no score but Q is still 2, so coverage drops to 1.05
    std::vector<SearchResult> r = engine.search("alpha zzz", 10, chunks, index);
    double idf = std::log(4.0 / 3.0) + 1.0;
    assert(r.size() == 2);
    assert(same(r[1].score, round12(idf * 1.05)));
}

static void query_dedup_and_order_test() {
    std::vector<Chunk> chunks = sample();
    CorpusIndex index(chunks);
    RetrievalEngine engine;

    std::vector<SearchResult> once = engine.search("alpha", 10, chunks, index);
    std::vector<SearchResult> many = engine.search("alpha ALPHA, alpha", 10, chunks, index);
    assert(once.size() == many.size());
    for (std::size_t i = 0; i < once.size(); ++i) {
        assert(once[i].score == many[i].score);
    }

    // term order in the query doesnt change scores at all, not even the last bit
    std::vector<SearchResult> ab = engine.search("alpha beta gamma", 10, chunks, index);
    std::vector<SearchResult> ba = engine.search("gamma beta alpha", 10, chunks, index);
    assert(ab.size() == ba.size());
    for (std::size_t i = 0; i < ab.size(); ++i) {
        assert(ab[i].chunk_id == ba[i].chunk_id);
        assert(ab[i].score == ba[i].score);
    }
}

static void tie_break_test() {
    // same text everywhere so every score ties. vector order is scrambled on
    // purpose, results have to follow document order then sequence
    std::vector<Chunk> chunks = {
        make_chunk("late", 2, 0, "tie"),
        make_chunk("mid", 1, 1, "tie"),
        make_chunk("early", 0, 0, "tie"),
        make_chunk("mid", 1, 0, "tie"),
    };
    CorpusIndex index(chunks);
    RetrievalEngine engine;

    std::vector<SearchResult> r = engine.search("tie", 10, chunks, index);
    assert(r.size() == 4);
    assert(r[0].chunk_id == "early#0");
    assert(r[1].chunk_id == "mid#0");
    assert(r[2].chunk_id == "mid#1");
    assert(r[3].chunk_id == "late#0");
    for (const SearchResult& x : r) {
        assert(x.score == r[0].score);
    }

    // k cuts after ordering, not before
    std::vector<SearchResult> top2 = engine.search("tie", 2, chunks, index);
    assert(top2.size() == 2);
    assert(top2[0].chunk_id == "early#0" && top2[1].chunk_id == "mid#0");
}

static void k_limit_test() {
    std::vector<Chunk> chunks = sample();
    CorpusIndex index(chunks);
    RetrievalEngine engine;

    std::vector<SearchResult> top1 = engine.search("alpha", 1, chunks, index);
    assert(top1.size() == 1 && top1[0].chunk_id == "a#0");

    // k bigger than the candidate count just returns every candidate
    assert(engine.search("alpha", 1000, chunks, index).size() == 2);
}

static void canonical_and_deterministic_test() {
    std::vector<Chunk> chunks;
    for (std::size_t d = 0; d < 5; ++d) {
        for (std::size_t s = 0; s < 3; ++s) {
            std::string text = "common";
            for (std::size_t i = 0; i <= d + s; ++i) {
                text += " rare" + std::to_string(i % 3);
            }
            chunks.push_back(make_chunk("doc" + std::to_string(d), d, s, text));
        }
    }
    CorpusIndex index(chunks);
    RetrievalEngine engine;

    std::vector<SearchResult> a = engine.search("common rare0 rare2", 20, chunks, index);
    std::vector<SearchResult> b = engine.search("common rare0 rare2", 20, chunks, index);
    assert(a.size() == chunks.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        assert(a[i].chunk_id == b[i].chunk_id);
        // stored score already has at most 12 decimals
        assert(a[i].score == round12(a[i].score));
        // never increasing down the list
        if (i > 0) {
            assert(a[i - 1].score >= a[i].score);
        }
    }
}

int main() {
    empty_query_test();
    negative_k_test();
    single_term_score_test();
    coverage_test();
    unknown_term_counts_in_q_test();
    query_dedup_and_order_test();
    tie_break_test();
    k_limit_test();
    canonical_and_deterministic_test();
    std::cout << "retrieval_engine tests passed\n";
    return 0;
}