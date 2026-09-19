#include "aiws/chunker.hpp"

#include <stdexcept>

namespace aiws {

Chunker::Chunker(ChunkingPolicy policy) : policy_(policy) {
    if (policy_.max_tokens == 0 || policy_.overlap >= policy_.max_tokens ||
        policy_.paragraph_window > policy_.max_tokens) {
        throw std::invalid_argument("invalid chunking policy");
    }
}

std::vector<Chunk> Chunker::chunk(const Document&, std::size_t) const {
    // TODO: produce deterministic, source-attributed chunks for the supplied document.
    return {};
}

}  // namespace aiws
