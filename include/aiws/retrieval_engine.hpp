#pragma once

#include "aiws/corpus_index.hpp"
#include "aiws/processing_types.hpp"

#include <string>
#include <vector>

namespace aiws {

class RetrievalEngine {
public:
    std::vector<SearchResult> search(const std::string& query,
                                     int k,
                                     const std::vector<Chunk>& chunks,
                                     const CorpusIndex& index) const;

private:
    static double canonical_score(double value);
};

}  // namespace aiws
