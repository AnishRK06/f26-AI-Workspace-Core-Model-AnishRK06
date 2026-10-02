#pragma once

#include "aiws/document.hpp"
#include "aiws/processing_types.hpp"

#include <cstddef>
#include <vector>

namespace aiws {

// Abstract interface for turning one document into ordered chunks.
// ProcessingCore::rebuild only knows this type; the object behind it can be
// Chunker (the default) or any caller-supplied derived class.
class ChunkingStrategy {
public:
    // virtual so that deleting through unique_ptr<ChunkingStrategy> runs the
    // derived destructor too, not just this one
    virtual ~ChunkingStrategy() = default;

    // pure virtual: makes the class abstract and forces every derived
    // strategy to provide its own chunking
    virtual std::vector<Chunk> chunk(const Document& document,
                                     std::size_t document_order) const = 0;

protected:
    // only derived classes can construct/copy/move the base part. stops
    // slicing through a base reference (e.g. *a = *b on two different
    // derived strategies) while still letting derived classes be copyable
    ChunkingStrategy() = default;
    ChunkingStrategy(const ChunkingStrategy&) = default;
    ChunkingStrategy& operator=(const ChunkingStrategy&) = default;
    ChunkingStrategy(ChunkingStrategy&&) = default;
    ChunkingStrategy& operator=(ChunkingStrategy&&) = default;
};

}  // namespace aiws
