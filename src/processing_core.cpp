#include "aiws/processing_core.hpp"

#include "aiws/chunker.hpp"
#include "aiws/context_builder.hpp"
#include "aiws/corpus_index.hpp"
#include "aiws/retrieval_engine.hpp"
#include "aiws/text_processor.hpp"

#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace aiws {

// ProcessingCore's private state. The core owns exactly one strategy per role
// through a unique_ptr to the base interface, so it never names the concrete
// type it is calling. chunks + index are the corpus and are only ever
// replaced together in rebuild.
struct ProcessingCore::Impl {
    std::unique_ptr<ChunkingStrategy> chunking;
    std::unique_ptr<RetrievalStrategy> retrieval;
    std::unique_ptr<ContextStrategy> context;

    std::vector<Chunk> chunks;
    CorpusIndex index;

    Impl(std::unique_ptr<ChunkingStrategy> c,
         std::unique_ptr<RetrievalStrategy> r,
         std::unique_ptr<ContextStrategy> x)
        : chunking(std::move(c)), retrieval(std::move(r)), context(std::move(x)) {}
};

// the commit step in rebuild relies on these moves not throwing
static_assert(std::is_nothrow_move_assignable<std::vector<Chunk>>::value,
              "chunk commit must not throw");
static_assert(std::is_nothrow_move_assignable<CorpusIndex>::value,
              "index commit must not throw");

namespace {

// a moved-from core has impl_ == nullptr. reads on it act like an empty
// corpus instead of dereferencing null
const std::vector<Chunk>& empty_chunks() {
    static const std::vector<Chunk> empty;
    return empty;
}

// public term arguments must normalize to exactly one token
// returns false for an empty term (caller returns 0)
bool single_term(const std::string& term, std::string& out) {
    auto terms = TextProcessor::terms(term);
    if (terms.empty()) return false;
    if (terms.size() != 1) throw std::invalid_argument("term must normalize to one token");
    out = std::move(terms.front());
    return true;
}

}  // namespace

// default configuration: the M1 classes, installed through the same
// interfaces an injected strategy would use
ProcessingCore::ProcessingCore()
    : impl_(std::make_unique<Impl>(
          std::make_unique<Chunker>(ChunkingPolicy{kMaxChunkTokens, kChunkOverlap,
                                                   kParagraphPreferenceWindow}),
          std::make_unique<RetrievalEngine>(),
          std::make_unique<ContextBuilder>())) {}

// caller-supplied configuration. the parameters are taken by value, so the
// caller's std::move already handed ownership to them. if we throw here,
// the parameters are destroyed on the way out and any non-null strategies are
// freed exactly once - nothing leaks and nothing goes back to the caller
ProcessingCore::ProcessingCore(std::unique_ptr<ChunkingStrategy> chunking,
                               std::unique_ptr<RetrievalStrategy> retrieval,
                               std::unique_ptr<ContextStrategy> context) {
    if (!chunking) throw std::invalid_argument("chunking strategy must not be null");
    if (!retrieval) throw std::invalid_argument("retrieval strategy must not be null");
    if (!context) throw std::invalid_argument("context strategy must not be null");
    impl_ = std::make_unique<Impl>(std::move(chunking), std::move(retrieval),
                                   std::move(context));
}

// unique_ptr<Impl> handles all of these. Impl is complete in this file, so
// the defaulted destructor/moves can be generated here (pimpl rule)
ProcessingCore::~ProcessingCore() = default;
ProcessingCore::ProcessingCore(ProcessingCore&&) noexcept = default;
ProcessingCore& ProcessingCore::operator=(ProcessingCore&&) noexcept = default;

std::string ProcessingCore::normalize(const std::string& text) {
    return TextProcessor::normalize(text);
}

void ProcessingCore::rebuild(const Workspace& workspace) {
    // a moved-from core gave its strategies away. silently installing the
    // defaults would change the configuration behind the caller's back
    if (!impl_) throw std::logic_error("rebuild on a moved-from ProcessingCore");

    // phase 1+2: validate ids and build the new corpus off to the side.
    // anything that throws here (duplicate id, a custom strategy throwing,
    // duplicate chunk ids in CorpusIndex, bad_alloc) leaves impl_ untouched
    std::unordered_set<std::string> document_ids;
    std::vector<Chunk> next_chunks;
    const auto& docs = workspace.documents();
    for (std::size_t order = 0; order < docs.size(); ++order) {
        const auto& doc = docs[order];
        if (!document_ids.insert(doc.id()).second) {
            throw std::invalid_argument("duplicate document id: " + doc.id());
        }
        // virtual dispatch: runs Chunker::chunk by default, or the injected
        // strategy's chunk
        auto produced = impl_->chunking->chunk(doc, order);
        next_chunks.insert(next_chunks.end(),
                           std::make_move_iterator(produced.begin()),
                           std::make_move_iterator(produced.end()));
    }
    CorpusIndex next_index(next_chunks);

    // phase 3: commit. both are noexcept moves, so the corpus is replaced as
    // a pair. the strategies are not touched by rebuild at all
    impl_->chunks = std::move(next_chunks);
    impl_->index = std::move(next_index);
}

const std::vector<Chunk>& ProcessingCore::chunks() const noexcept {
    return impl_ ? impl_->chunks : empty_chunks();
}

std::size_t ProcessingCore::chunk_count() const noexcept {
    return impl_ ? impl_->chunks.size() : 0;
}

std::size_t ProcessingCore::document_frequency(const std::string& term) const {
    std::string t;
    if (!single_term(term, t) || !impl_) return 0;
    return impl_->index.document_frequency(t);
}

std::size_t ProcessingCore::term_frequency(const std::string& term,
                                           const std::string& chunk_id) const {
    std::string t;
    if (!single_term(term, t) || !impl_) return 0;
    return impl_->index.term_frequency(t, chunk_id);
}

std::vector<SearchResult> ProcessingCore::search(const std::string& query, int k) const {
    if (!impl_) {
        if (k < 0) throw std::invalid_argument("k must be non-negative");
        return {};
    }
    // virtual dispatch: RetrievalEngine::search by default. the strategy
    // borrows chunks and index by const& for this call only
    return impl_->retrieval->search(query, k, impl_->chunks, impl_->index);
}

std::vector<ContextItem> ProcessingCore::build_context(const std::string& query,
                                                       int k,
                                                       std::size_t token_budget) const {
    if (k < 0) throw std::invalid_argument("k must be non-negative");
    if (token_budget == 0 || !impl_) return {};
    // ranked results come from the configured retrieval strategy, then go to
    // the configured context strategy (ContextBuilder::build by default)
    return impl_->context->build(search(query, k), token_budget);
}

}  // namespace aiws
