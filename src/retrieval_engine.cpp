#include "aiws/retrieval_engine.hpp"

namespace aiws {

double RetrievalEngine::canonical_score(double) {
    // TODO: return the score in the required canonical form.
    
    return 0.0;
}

std::vector<SearchResult> RetrievalEngine::search(const std::string&,
                                                  int,
                                                  const std::vector<Chunk>&,
                                                  const CorpusIndex&) const {
    // TODO: return the ranked search results for the requested query.
    
    return {};
}

}  // namespace aiws

