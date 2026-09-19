#include "aiws/retrieval_engine.hpp"

#include "aiws/text_processor.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace aiws {

namespace {

constexpr double kCoverageWeight = 0.10;

// running totals for one chunk that matched at least one query term
struct Candidate {
    double base = 0.0;
    std::size_t matched = 0;
};

// what the sort needs, text and ids get copied only for the top k
struct Ranked {
    std::size_t chunk_index;
    double score;
    std::size_t matched;
};

// sorted and deduped so "b a" and "a b a" score exactly the same as "a b"
std::vector<std::string> unique_terms(const std::string& query) {
    std::vector<std::string> terms = TextProcessor::terms(query);
    std::sort(terms.begin(), terms.end());
    terms.erase(std::unique(terms.begin(), terms.end()), terms.end());
    return terms;
}

}  // namespace

// rounded to 12 decimals so scores that only differ by float noise compare equal
double RetrievalEngine::canonical_score(double value) {
    return std::round(value * 1e12) / 1e12;
}

std::vector<SearchResult> RetrievalEngine::search(const std::string& query,
                                                  int k,
                                                  const std::vector<Chunk>& chunks,
                                                  const CorpusIndex& index) const {
    if (k < 0) {
        throw std::invalid_argument("k must not be negative");
    }

    std::vector<std::string> terms = unique_terms(query);
    if (k == 0 || terms.empty() || chunks.empty()) {
        return {};
    }

    const double n = static_cast<double>(chunks.size());
    const double q = static_cast<double>(terms.size());

    // only chunks in some postings list ever get touched, nothing rescans text
    std::unordered_map<std::size_t, Candidate> candidates;
    for (const std::string& term : terms) {
        const std::vector<CorpusIndex::Posting>* list = index.postings(term);
        if (list == nullptr) {
            // unknown term, still counted in q but adds nothing
            continue;
        }

        const double df = static_cast<double>(list->size());
        const double idf = std::log((n + 1.0) / (df + 1.0)) + 1.0;

        for (const CorpusIndex::Posting& p : *list) {
            if (p.chunk_index >= chunks.size()) {
                continue;
            }
            const double tf = 1.0 + std::log(static_cast<double>(p.frequency));
            Candidate& c = candidates[p.chunk_index];
            c.base += tf * idf;
            ++c.matched;
        }
    }

    std::vector<Ranked> ranked;
    ranked.reserve(candidates.size());
    for (const auto& entry : candidates) {
        const Candidate& c = entry.second;
        double coverage = 1.0 + kCoverageWeight * static_cast<double>(c.matched) / q;
        ranked.push_back(Ranked{entry.first, canonical_score(c.base * coverage), c.matched});
    }

    // score desc, then document order, then chunk sequence. chunk_index last
    // so the order is total and never depends on the hash map
    auto better = [&chunks](const Ranked& a, const Ranked& b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        const Chunk& ca = chunks[a.chunk_index];
        const Chunk& cb = chunks[b.chunk_index];
        if (ca.document_order != cb.document_order) {
            return ca.document_order < cb.document_order;
        }
        if (ca.sequence != cb.sequence) {
            return ca.sequence < cb.sequence;
        }
        return a.chunk_index < b.chunk_index;
    };

    // only the top k need to be in order
    std::size_t limit = std::min(ranked.size(), static_cast<std::size_t>(k));
    std::partial_sort(ranked.begin(), ranked.begin() + limit, ranked.end(), better);

    std::vector<SearchResult> results;
    results.reserve(limit);
    for (std::size_t i = 0; i < limit; ++i) {
        const Chunk& chunk = chunks[ranked[i].chunk_index];
        SearchResult r;
        r.chunk_id = chunk.id;
        r.document_id = chunk.document_id;
        r.chunk_sequence = chunk.sequence;
        r.text = chunk.text;
        r.score = ranked[i].score;
        r.matched_terms = ranked[i].matched;
        results.push_back(std::move(r));
    }
    return results;
}

}  // namespace aiws