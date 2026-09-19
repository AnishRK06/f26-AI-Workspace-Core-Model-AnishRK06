#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "aiws/chunker.hpp"
#include "aiws/document.hpp"
#include "aiws/text_processor.hpp"

using aiws::Chunk;
using aiws::Chunker;
using aiws::ChunkingPolicy;
using aiws::Document;
using aiws::TextProcessor;

// "w<from> ... w<to-1>" so every token is unique and easy to find
static std::string words(int from, int to) {
    std::string s;
    for (int i = from; i < to; ++i) {
        if (!s.empty()) {
            s += ' ';
        }
        s += "w" + std::to_string(i);
    }
    return s;
}

// first token of a chunk, tells us exactly where it started
static std::string first_word(const Chunk& c) {
    return c.text.substr(0, c.text.find(' '));
}

static std::vector<Chunk> chunk_text(const std::string& text) {
    Chunker chunker;
    return chunker.chunk(Document{"d", "D", text}, 0);
}

static void empty_test() {
    assert(chunk_text("").empty());
    assert(chunk_text("  \n\n\t ").empty());
    assert(chunk_text("...!!!---").empty());
}

static void short_document_test() {
    std::string text = "  Hello, World!\n\nSecond paragraph.  ";
    Chunker chunker;
    std::vector<Chunk> chunks = chunker.chunk(Document{"doc", "Doc", text}, 3);

    // paragraphs dont split a document that already fits
    assert(chunks.size() == 1);
    const Chunk& c = chunks[0];
    assert(c.id == "doc#0");
    assert(c.document_id == "doc");
    assert(c.document_order == 3);
    assert(c.sequence == 0);
    assert(c.text == "hello world second paragraph");
    assert(c.token_count == 4);

    // span skips the leading spaces and stops at the end of the last token
    assert(c.source_begin == 2);
    assert(text.substr(c.source_begin, c.source_end - c.source_begin) ==
           "Hello, World!\n\nSecond paragraph");
}

static void size_boundary_test() {
    // exactly max is one chunk
    std::vector<Chunk> c120 = chunk_text(words(0, 120));
    assert(c120.size() == 1);
    assert(c120[0].token_count == 120);

    // one over max gives a 21 token tail that starts 20 back
    std::vector<Chunk> c121 = chunk_text(words(0, 121));
    assert(c121.size() == 2);
    assert(c121[0].token_count == 120);
    assert(c121[1].token_count == 21);
    assert(first_word(c121[1]) == "w100");
    assert(c121[1].id == "d#1");
    assert(c121[1].sequence == 1);

    // 240 tokens: [0,120) [100,220) [200,240)
    std::vector<Chunk> c240 = chunk_text(words(0, 240));
    assert(c240.size() == 3);
    assert(c240[1].token_count == 120 && first_word(c240[1]) == "w100");
    assert(c240[2].token_count == 40 && first_word(c240[2]) == "w200");
}

static void overlap_text_test() {
    std::vector<Chunk> chunks = chunk_text(words(0, 300));
    for (std::size_t i = 1; i < chunks.size(); ++i) {
        std::vector<std::string> prev = TextProcessor::terms(chunks[i - 1].text);
        std::vector<std::string> cur = TextProcessor::terms(chunks[i].text);
        // last 20 of the previous chunk are the first 20 of this one
        for (std::size_t k = 0; k < 20; ++k) {
            assert(prev[prev.size() - 20 + k] == cur[k]);
        }
    }
}

static void paragraph_preference_test() {
    // break before token 110, inside the window
    std::vector<Chunk> c = chunk_text(words(0, 110) + "\n\n" + words(110, 200));
    assert(c.size() == 2);
    assert(c[0].token_count == 110);
    assert(first_word(c[1]) == "w90");

    // break before token 100 is the edge of the window and still counts
    c = chunk_text(words(0, 100) + "\n\n" + words(100, 200));
    assert(c[0].token_count == 100);
    assert(first_word(c[1]) == "w80");

    // break before token 99 is outside the window, hard cut at 120
    c = chunk_text(words(0, 99) + "\n\n" + words(99, 200));
    assert(c[0].token_count == 120);

    // break before token 121 is past max, hard cut at 120
    c = chunk_text(words(0, 121) + "\n\n" + words(121, 200));
    assert(c[0].token_count == 120);

    // two breaks in the window, the later one wins
    c = chunk_text(words(0, 105) + "\n\n" + words(105, 115) + "\n\n" + words(115, 200));
    assert(c[0].token_count == 115);

    // crlf blank line behaves the same
    c = chunk_text(words(0, 110) + "\r\n\r\n" + words(110, 200));
    assert(c[0].token_count == 110);

    // single newline is not a paragraph
    c = chunk_text(words(0, 110) + "\n" + words(110, 200));
    assert(c[0].token_count == 120);
}

static void span_test() {
    std::string text = "Intro: " + words(0, 110) + ".\n\n" + words(110, 250) + " END";
    std::vector<Chunk> chunks = chunk_text(text);
    assert(chunks.size() >= 2);

    for (std::size_t i = 0; i < chunks.size(); ++i) {
        const Chunk& c = chunks[i];
        assert(c.source_begin < c.source_end);
        assert(c.source_end <= text.size());
        // normalizing the original span gives back exactly the chunk text
        std::string original = text.substr(c.source_begin, c.source_end - c.source_begin);
        assert(TextProcessor::normalize(original) == c.text);
        assert(TextProcessor::terms(c.text).size() == c.token_count);
        if (i > 0) {
            // overlap means spans overlap too, and they move forward
            assert(c.source_begin < chunks[i - 1].source_end);
            assert(c.source_begin > chunks[i - 1].source_begin);
        }
    }
}

static void deterministic_test() {
    Document doc{"same", "Same", words(0, 110) + "\n\n" + words(110, 400)};
    std::string before = doc.text();
    Chunker chunker;
    std::vector<Chunk> a = chunker.chunk(doc, 1);
    std::vector<Chunk> b = chunker.chunk(doc, 1);

    assert(doc.text() == before);
    assert(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        assert(a[i].id == b[i].id);
        assert(a[i].text == b[i].text);
        assert(a[i].source_begin == b[i].source_begin);
        assert(a[i].source_end == b[i].source_end);
        assert(a[i].sequence == i);
    }
}

static void custom_policy_test() {
    // small numbers make the rules easy to check by hand
    Chunker small(ChunkingPolicy{10, 2, 3});
    std::vector<Chunk> c = small.chunk(Document{"s", "S", words(0, 25)}, 0);
    // [0,10) [8,18) [16,25)
    assert(c.size() == 3);
    assert(first_word(c[1]) == "w8");
    assert(first_word(c[2]) == "w16");
    assert(c[2].token_count == 9);

    bool threw = false;
    try {
        Chunker bad(ChunkingPolicy{10, 10, 3});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);
}

int main() {
    empty_test();
    short_document_test();
    size_boundary_test();
    overlap_text_test();
    paragraph_preference_test();
    span_test();
    deterministic_test();
    custom_policy_test();
    std::cout << "chunker tests passed\n";
    return 0;
}