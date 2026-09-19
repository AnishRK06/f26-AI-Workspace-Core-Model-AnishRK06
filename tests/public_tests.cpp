#include "aiws/processing_core.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int failures = 0;
void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
std::string numbered_words(int n) {
    std::string s;
    for (int i = 0; i < n; ++i) {
        if (!s.empty()) s += ' ';
        s += "w" + std::to_string(i);
    }
    return s;
}
}

int main() {
    using namespace aiws;

    check(ProcessingCore::normalize("Hello,  WORLD! 2026") == "hello world 2026",
          "normalization contract");
    check(ProcessingCore::normalize("...\t---").empty(), "separator-only input");

    Workspace ws;
    ws.add_document(Document{"d1", "One", "alpha alpha beta"});
    ws.add_document(Document{"d2", "Two", "alpha gamma"});
    ProcessingCore core;
    core.rebuild(ws);
    check(core.chunk_count() == 2, "one short chunk per document");
    check(core.document_frequency("ALPHA!") == 2, "DF is number of chunks containing term");
    check(core.term_frequency("alpha", "d1#0") == 2, "term frequency retained in posting");

    auto r = core.search("alpha beta", 10);
    check(r.size() == 2, "query returns candidate union");
    check(!r.empty() && r[0].document_id == "d1", "coverage/frequency ranking");

    auto ctx = core.build_context("alpha beta", 10, 2);
    check(ctx.size() == 1 && ctx[0].token_count == 2 && ctx[0].truncated,
          "context truncates final selected chunk at token budget");

    Workspace long_ws;
    long_ws.add_document(Document{"long", "Long", numbered_words(121)});
    core.rebuild(long_ws);
    check(core.chunk_count() == 2, "121 tokens creates overlapping second chunk");
    check(core.chunks().size() >= 2 &&
              core.chunks()[0].token_count == 120 && core.chunks()[1].token_count == 21,
          "hard limit and 20-token overlap");

    bool negative_threw = false;
    try { (void)core.search("w1", -1); }
    catch (const std::invalid_argument&) { negative_threw = true; }
    check(negative_threw, "negative k throws invalid_argument");

    if (failures == 0) {
        std::cout << "All public tests passed.\n";
        return 0;
    }
    std::cerr << failures << " public test(s) failed.\n";
    return 1;
}
