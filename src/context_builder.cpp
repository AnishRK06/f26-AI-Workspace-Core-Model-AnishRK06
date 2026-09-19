#include "aiws/context_builder.hpp"

#include "aiws/text_processor.hpp"

#include <string>
#include <unordered_set>
#include <utility>

namespace aiws {

// takes results already in ranked order and never reorders them,
// ranking belongs to the retrieval engine
std::vector<ContextItem> ContextBuilder::build(const std::vector<SearchResult>& ranked,
                                               std::size_t token_budget) const {
    std::vector<ContextItem> items;
    std::unordered_set<std::string> used_ids;
    std::size_t used = 0;

    for (const SearchResult& result : ranked) {
        if (used == token_budget) {
            break;
        }
        // same chunk twice would waste budget on repeated text
        if (!used_ids.insert(result.chunk_id).second) {
            continue;
        }

        // budget counts content tokens only, ids and metadata are free
        std::vector<std::string> tokens = TextProcessor::terms(result.text);
        if (tokens.empty()) {
            continue;
        }

        ContextItem item;
        item.chunk_id = result.chunk_id;
        item.document_id = result.document_id;
        item.chunk_sequence = result.chunk_sequence;
        item.score = result.score;

        std::size_t remaining = token_budget - used;
        if (tokens.size() <= remaining) {
            item.text = TextProcessor::join(tokens, 0, tokens.size());
            item.token_count = tokens.size();
            item.truncated = false;
        } else {
            // partial chunk goes in last, nothing after it
            item.text = TextProcessor::join(tokens, 0, remaining);
            item.token_count = remaining;
            item.truncated = true;
        }

        used += item.token_count;
        bool stop = item.truncated;
        items.push_back(std::move(item));
        if (stop) {
            break;
        }
    }

    return items;
}

}  // namespace aiws