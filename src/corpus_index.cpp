#include "aiws/corpus_index.hpp"

namespace aiws {

CorpusIndex::CorpusIndex(const std::vector<Chunk>& chunks) {
    build(chunks);
}

void CorpusIndex::build(const std::vector<Chunk>&) {
    // TODO: build the searchable index from the supplied chunks.
}

std::size_t CorpusIndex::document_frequency(
    const std::string&) const noexcept {
    // TODO: return how many chunks contain the requested term.
    return 0;
}

std::size_t CorpusIndex::term_frequency(
    const std::string&,
    const std::string&) const noexcept {
    // TODO: return the requested term's frequency in the specified chunk.
    return 0;
}

const std::vector<CorpusIndex::Posting>* CorpusIndex::postings(
    const std::string&) const noexcept {
    // TODO: return the postings associated with the requested term.
    return nullptr;
}

const Chunk* CorpusIndex::find_chunk(
    const std::vector<Chunk>&,
    const std::string&) const noexcept {
    // TODO: find the chunk identified by the requested chunk ID.
    return nullptr;
}

std::size_t CorpusIndex::chunk_index(const std::string&) const {
    // TODO: return the stored index of the requested chunk ID.
    return 0;
}

}  // namespace aiws
