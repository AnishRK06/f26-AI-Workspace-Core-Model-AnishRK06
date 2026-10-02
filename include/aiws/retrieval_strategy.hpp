#pragma once

#include "aiws/corpus_index.hpp"
#include "aiws/processing_types.hpp"

#include <string>
#include <vector>

namespace aiws {

// Abstract interface for ranked retrieval over the current corpus.
// ProcessingCore::search borrows its chunks and index to the strategy for
// the length of one call; the strategy never owns or keeps them.
class RetrievalStrategy {
public:
    // virtual so deleting through unique_ptr<RetrievalStrategy> is safe
    virtual ~RetrievalStrategy() = default;

    // pure virtual: abstract class, every derived strategy must implement it
    virtual std::vector<SearchResult> search(const std::string& query,
                                             int k,
                                             const std::vector<Chunk>& chunks,
                                             const CorpusIndex& index) const = 0;

protected:
    // blocks slicing through base references, derived classes stay copyable
    RetrievalStrategy() = default;
    RetrievalStrategy(const RetrievalStrategy&) = default;
    RetrievalStrategy& operator=(const RetrievalStrategy&) = default;
    RetrievalStrategy(RetrievalStrategy&&) = default;
    RetrievalStrategy& operator=(RetrievalStrategy&&) = default;
};

}  // namespace aiws
