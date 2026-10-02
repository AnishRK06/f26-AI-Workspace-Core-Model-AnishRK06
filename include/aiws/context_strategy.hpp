#pragma once

#include "aiws/processing_types.hpp"

#include <cstddef>
#include <vector>

namespace aiws {

// Abstract interface for turning ranked results into a bounded context.
class ContextStrategy {
public:
    // virtual so deleting through unique_ptr<ContextStrategy> is safe
    virtual ~ContextStrategy() = default;

    // pure virtual: abstract class, every derived strategy must implement it
    virtual std::vector<ContextItem> build(const std::vector<SearchResult>& ranked,
                                           std::size_t token_budget) const = 0;

protected:
    // blocks slicing through base references, derived classes stay copyable
    ContextStrategy() = default;
    ContextStrategy(const ContextStrategy&) = default;
    ContextStrategy& operator=(const ContextStrategy&) = default;
    ContextStrategy(ContextStrategy&&) = default;
    ContextStrategy& operator=(ContextStrategy&&) = default;
};

}  // namespace aiws
