#pragma once

#include "aiws/processing_types.hpp"

#include <cstddef>
#include <vector>

namespace aiws {

// M2 PUBLIC-INTERFACE DESIGN TASK
// Complete this class as a safe abstract polymorphic interface.
// Keep the class name, operation name, parameter types, return type,
// const qualification, and namespace unchanged.
class ContextStrategy {
public:
    // TODO: make destruction safe through a base-class pointer.
    ~ContextStrategy() = default;

    // TODO: make this a required polymorphic operation.
    virtual std::vector<ContextItem> build(const std::vector<SearchResult>&,
                                           std::size_t) const {
        return {};
    }
};

}  // namespace aiws
