#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "aiws/text_processor.hpp"

using aiws::TextProcessor;
using aiws::TokenInfo;

static void normalize_test() {
    // spec examples
    assert(TextProcessor::normalize("Hello, WORLD!") == "hello world");
    assert(TextProcessor::normalize("R2-D2") == "r2 d2");
    assert(TextProcessor::normalize("...!?").empty());

    // separator runs, leading and trailing separators
    assert(TextProcessor::normalize("  a\t\t,,b  \n") == "a b");
    assert(TextProcessor::normalize("").empty());
    assert(TextProcessor::normalize(" \n\r\n\t ").empty());

    // digits kept, mixed case inside one token
    assert(TextProcessor::normalize("ECE3574 FL26") == "ece3574 fl26");

    // high-bit bytes are separators, not letters (utf8 e-acute here)
    assert(TextProcessor::normalize("caf\xC3\xA9 ok") == "caf ok");
    assert(TextProcessor::normalize("a\xFF" "b") == "a b");

    // embedded null byte is a separator too
    assert(TextProcessor::normalize(std::string("a\0b", 3)) == "a b");

    // normalizing twice changes nothing
    std::string once = TextProcessor::normalize("The QUICK, brown-fox!!");
    assert(TextProcessor::normalize(once) == once);
}

static void terms_test() {
    std::vector<std::string> t = TextProcessor::terms("Alpha alpha, BETA");
    assert(t.size() == 3);
    assert(t[0] == "alpha" && t[1] == "alpha" && t[2] == "beta");
    assert(TextProcessor::terms("--").empty());
}

static void span_test() {
    std::string text = "  Hello,\tWORLD-42!";
    std::vector<TokenInfo> tokens = TextProcessor::tokenize(text);
    assert(tokens.size() == 3);

    // spans point into the original text, original casing still there
    assert(tokens[0].begin == 2 && tokens[0].end == 7);
    assert(text.substr(tokens[0].begin, tokens[0].end - tokens[0].begin) == "Hello");
    assert(text.substr(tokens[1].begin, tokens[1].end - tokens[1].begin) == "WORLD");
    assert(tokens[2].token == "42");
    assert(tokens[2].end == 17);

    // every span is non-empty and in order
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        assert(tokens[i].begin < tokens[i].end);
        if (i > 0) {
            assert(tokens[i - 1].end <= tokens[i].begin);
        }
    }
}

static std::vector<std::size_t> paragraphs_of(const std::string& text) {
    std::vector<std::size_t> result;
    for (const TokenInfo& t : TextProcessor::tokenize(text)) {
        result.push_back(t.paragraph);
    }
    return result;
}

static void paragraph_test() {
    using P = std::vector<std::size_t>;

    // single newline is not a boundary
    assert(paragraphs_of("a\nb") == (P{0, 0}));

    // blank line is
    assert(paragraphs_of("a\n\nb") == (P{0, 1}));

    // spaces and tabs on the blank line still count
    assert(paragraphs_of("a\n \t \nb") == (P{0, 1}));

    // crlf behaves the same as lf
    assert(paragraphs_of("a\r\nb") == (P{0, 0}));
    assert(paragraphs_of("a\r\n\r\nb") == (P{0, 1}));
    assert(paragraphs_of("a\r\n \t\r\nb") == (P{0, 1}));

    // a line with punctuation is not blank
    assert(paragraphs_of("a\n.\nb") == (P{0, 0}));

    // many blank lines are one boundary
    assert(paragraphs_of("a\n\n\n\n\nb") == (P{0, 1}));

    // leading blank lines dont bump the first paragraph
    assert(paragraphs_of("\n\n\na b") == (P{0, 0}));

    // boundary followed only by punctuation, then a real token
    assert(paragraphs_of("a\n\n--- b\n\nc") == (P{0, 1, 2}));

    // paragraph info doesnt change the normalized text
    assert(TextProcessor::normalize("a\n\nb") == TextProcessor::normalize("a b"));
}

static void join_test() {
    std::vector<std::string> words = {"a", "b", "c", "d"};
    assert(TextProcessor::join(words, 0, 4) == "a b c d");
    assert(TextProcessor::join(words, 1, 3) == "b c");
    assert(TextProcessor::join(words, 2, 2).empty());
    assert(TextProcessor::join(words, 3, 1).empty());
    assert(TextProcessor::join(words, 2, 100) == "c d");

    std::vector<TokenInfo> tokens = TextProcessor::tokenize("One, TWO; three");
    assert(TextProcessor::join(tokens, 0, tokens.size()) == "one two three");
    assert(TextProcessor::join(tokens, 1, 2) == "two");
}

int main() {
    normalize_test();
    terms_test();
    span_test();
    paragraph_test();
    join_test();
    std::cout << "text_processor tests passed\n";
    return 0;
}