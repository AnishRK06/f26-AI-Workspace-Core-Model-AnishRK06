#include "aiws/context_builder.hpp"

namespace aiws {

std::vector<ContextItem> ContextBuilder::build(const std::vector<SearchResult>&, std::size_t) const {
    // TODO: assemble ranked results into context items within the supplied token budget.
    
    return {};
}

}  // namespace aiws
