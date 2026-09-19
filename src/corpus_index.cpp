#include "aiws/corpus_index.hpp"

#include "aiws/text_processor.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace aiws {

CorpusIndex::CorpusIndex(const std::vector<Chunk>& chunks) {
    build(chunks);
}

// builds into locals and swaps at the end, so if anything throws the
// old index is still there untouched
void CorpusIndex::build(const std::vector<Chunk>& chunks) {
    std::unordered_map<std::string, std::vector<Posting>> postings;
    std::unordered_map<std::string, std::size_t> chunk_by_id;

    for (std::size_t i = 0; i < chunks.size(); ++i) {
        const Chunk& chunk = chunks[i];
        if (!chunk_by_id.emplace(chunk.id, i).second) {
            throw std::invalid_argument("duplicate chunk id: " + chunk.id);
        }

        // count inside the chunk first so each term gets one posting per chunk
        std::unordered_map<std::string, std::size_t> counts;
        for (const std::string& term : TextProcessor::terms(chunk.text)) {
            ++counts[term];
        }
        // chunks are visited in order, so every postings list ends up
        // sorted by chunk_index without sorting it
        for (const auto& entry : counts) {
            postings[entry.first].push_back(Posting{i, entry.second});
        }
    }

    postings_.swap(postings);
    chunk_by_id_.swap(chunk_by_id);
}

std::size_t CorpusIndex::document_frequency(
    const std::string& normalized_term) const noexcept {
    const std::vector<Posting>* list = postings(normalized_term);
    return list == nullptr ? 0 : list->size();
}

std::size_t CorpusIndex::term_frequency(
    const std::string& normalized_term,
    const std::string& chunk_id) const noexcept {
    auto chunk = chunk_by_id_.find(chunk_id);
    const std::vector<Posting>* list = postings(normalized_term);
    if (chunk == chunk_by_id_.end() || list == nullptr) {
        return 0;
    }

    // postings are sorted by chunk_index, binary search instead of a scan
    auto it = std::lower_bound(
        list->begin(), list->end(), chunk->second,
        [](const Posting& p, std::size_t index) { return p.chunk_index < index; });
    if (it == list->end() || it->chunk_index != chunk->second) {
        return 0;
    }
    return it->frequency;
}

const std::vector<CorpusIndex::Posting>* CorpusIndex::postings(
    const std::string& normalized_term) const noexcept {
    auto it = postings_.find(normalized_term);
    return it == postings_.end() ? nullptr : &it->second;
}

// the index only stores positions, the caller owns the actual chunks
const Chunk* CorpusIndex::find_chunk(
    const std::vector<Chunk>& chunks,
    const std::string& chunk_id) const noexcept {
    auto it = chunk_by_id_.find(chunk_id);
    if (it == chunk_by_id_.end() || it->second >= chunks.size()) {
        return nullptr;
    }
    // guards against being handed a vector this index wasnt built from
    const Chunk& chunk = chunks[it->second];
    return chunk.id == chunk_id ? &chunk : nullptr;
}

std::size_t CorpusIndex::chunk_index(const std::string& chunk_id) const {
    auto it = chunk_by_id_.find(chunk_id);
    if (it == chunk_by_id_.end()) {
        throw std::out_of_range("unknown chunk id: " + chunk_id);
    }
    return it->second;
}

}  // namespace aiws