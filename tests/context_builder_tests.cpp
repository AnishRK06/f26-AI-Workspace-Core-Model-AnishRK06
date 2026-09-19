#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "aiws/context_builder.hpp"

using aiws::ContextBuilder;
using aiws::ContextItem;
using aiws::SearchResult;

static SearchResult make_result(const std::string& id, const std::string& text, double score) {
    SearchResult r;
    r.chunk_id = id;
    r.document_id = id.substr(0, id.find('#'));
    r.chunk_sequence = 0;
    r.text = text;
    r.score = score;
    r.matched_terms = 1;
    return r;
}

// 3, 4 and 2 tokens, already in ranked order
static std::vector<SearchResult> sample() {
    return {
        make_result("a#0", "one two three", 3.0),
        make_result("b#0", "four five six seven", 2.0),
        make_result("c#0", "eight nine", 1.0),
    };
}

static std::size_t total_tokens(const std::vector<ContextItem>& items) {
    std::size_t total = 0;
    for (const ContextItem& item : items) {
        total += item.token_count;
    }
    return total;
}

static void zero_and_empty_test() {
    ContextBuilder builder;
    assert(builder.build(sample(), 0).empty());
    assert(builder.build({}, 100).empty());
}

static void everything_fits_test() {
    ContextBuilder builder;
    std::vector<ContextItem> items = builder.build(sample(), 100);
    assert(items.size() == 3);
    assert(total_tokens(items) == 9);
    for (const ContextItem& item : items) {
        assert(!item.truncated);
    }

    // exact budget is still a full fit, not a truncation
    items = builder.build(sample(), 9);
    assert(items.size() == 3);
    assert(!items[2].truncated);
}

static void truncation_test() {
    ContextBuilder builder;

    // 3 fit, then 2 of the 4 from b#0
    std::vector<ContextItem> items = builder.build(sample(), 5);
    assert(items.size() == 2);
    assert(items[0].chunk_id == "a#0" && !items[0].truncated);
    assert(items[1].chunk_id == "b#0" && items[1].truncated);
    assert(items[1].text == "four five");
    assert(items[1].token_count == 2);
    assert(total_tokens(items) == 5);

    // budget smaller than the very first chunk
    items = builder.build(sample(), 1);
    assert(items.size() == 1);
    assert(items[0].text == "one");
    assert(items[0].truncated);

    // one token short of the last chunk
    items = builder.build(sample(), 8);
    assert(items.size() == 3);
    assert(items[2].text == "eight" && items[2].truncated);
}

static void stops_after_truncation_test() {
    // the second chunk doesnt fit, the third would fit in whats left,
    // but nothing is added after a truncated item
    std::vector<SearchResult> ranked = {
        make_result("a#0", "one two", 3.0),
        make_result("b#0", "three four five six", 2.0),
        make_result("c#0", "seven", 1.0),
    };
    ContextBuilder builder;
    std::vector<ContextItem> items = builder.build(ranked, 4);
    assert(items.size() == 2);
    assert(items[1].truncated);
    assert(items[1].token_count == 2);
}

static void attribution_and_order_test() {
    ContextBuilder builder;
    std::vector<SearchResult> ranked = sample();
    std::vector<ContextItem> items = builder.build(ranked, 100);

    // same order as the input and every field carried over
    for (std::size_t i = 0; i < items.size(); ++i) {
        assert(items[i].chunk_id == ranked[i].chunk_id);
        assert(items[i].document_id == ranked[i].document_id);
        assert(items[i].chunk_sequence == ranked[i].chunk_sequence);
        assert(items[i].score == ranked[i].score);
    }
}

static void duplicate_test() {
    std::vector<SearchResult> ranked = {
        make_result("a#0", "one two", 3.0),
        make_result("a#0", "one two", 3.0),
        make_result("b#0", "three", 2.0),
    };
    ContextBuilder builder;
    std::vector<ContextItem> items = builder.build(ranked, 100);
    assert(items.size() == 2);
    assert(items[0].chunk_id == "a#0" && items[1].chunk_id == "b#0");
    assert(total_tokens(items) == 3);
}

static void never_over_budget_test() {
    ContextBuilder builder;
    for (std::size_t budget = 0; budget <= 12; ++budget) {
        std::vector<ContextItem> items = builder.build(sample(), budget);
        assert(total_tokens(items) <= budget);
        // only the last item can be truncated
        for (std::size_t i = 0; i + 1 < items.size(); ++i) {
            assert(!items[i].truncated);
        }
    }
}

int main() {
    zero_and_empty_test();
    everything_fits_test();
    truncation_test();
    stops_after_truncation_test();
    attribution_and_order_test();
    duplicate_test();
    never_over_budget_test();
    std::cout << "context_builder tests passed\n";
    return 0;
}