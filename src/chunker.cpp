#include "aiws/chunker.hpp"

#include "aiws/text_processor.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace aiws {

Chunker::Chunker(ChunkingPolicy policy) : policy_(policy) {
    if (policy_.max_tokens == 0 || policy_.overlap >= policy_.max_tokens ||
        policy_.paragraph_window > policy_.max_tokens) {
        throw std::invalid_argument("invalid chunking policy");
    }
}

namespace {

// a boundary at relative position p means token p-1 and token p are in
// different paragraphs, so a chunk of length p would end on a paragraph
bool paragraph_break_before(const std::vector<TokenInfo>& tokens, std::size_t i) {
    return i > 0 && i < tokens.size() &&
           tokens[i - 1].paragraph != tokens[i].paragraph;
}

}  // namespace

std::vector<Chunk> Chunker::chunk(const Document& document,
                                  std::size_t document_order) const {
    std::vector<TokenInfo> tokens = TextProcessor::tokenize(document.text());
    std::vector<Chunk> chunks;

    // shortest chunk the paragraph rule is allowed to make, has to stay
    // above the overlap or the next start would never move forward
    std::size_t min_length = std::max(policy_.max_tokens - policy_.paragraph_window,
                                      policy_.overlap + 1);

    std::size_t start = 0;
    while (start < tokens.size()) {
        std::size_t remaining = tokens.size() - start;
        std::size_t length = policy_.max_tokens;

        if (remaining <= policy_.max_tokens) {
            // last chunk, keep it even if its short
            length = remaining;
        } else {
            // latest paragraph break wins, otherwise hard cut at max
            for (std::size_t p = policy_.max_tokens; p >= min_length; --p) {
                if (paragraph_break_before(tokens, start + p)) {
                    length = p;
                    break;
                }
            }
        }

        std::size_t end = start + length;

        Chunk c;
        c.sequence = chunks.size();
        c.id = document.id() + "#" + std::to_string(c.sequence);
        c.document_id = document.id();
        c.document_order = document_order;
        c.text = TextProcessor::join(tokens, start, end);
        c.token_count = length;
        // span is in the original text, not the normalized string
        c.source_begin = tokens[start].begin;
        c.source_end = tokens[end - 1].end;
        chunks.push_back(std::move(c));

        if (end == tokens.size()) {
            break;
        }
        start = end - policy_.overlap;
    }

    return chunks;
}

}  // namespace aiws