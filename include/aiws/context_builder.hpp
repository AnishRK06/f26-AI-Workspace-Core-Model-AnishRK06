#pragma once

#include "aiws/processing_types.hpp"

#include <cstddef>
#include <vector>

namespace aiws {

class ContextBuilder {
public:
    std::vector<ContextItem> build(const std::vector<SearchResult>& ranked,
                                   std::size_t token_budget) const;
};

}  // namespace aiws
