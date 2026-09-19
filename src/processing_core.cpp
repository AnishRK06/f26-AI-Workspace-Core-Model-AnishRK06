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

// everything that gets replaced together on a rebuild. the index stores
// positions into chunks, so the two only ever move as a pair
struct Corpus {
    std::vector<Chunk> chunks;
    CorpusIndex index;
};

// the commit step in rebuild relies on this
static_assert(std::is_nothrow_swappable<Corpus>::value,
              "swapping in a new corpus must not throw");

struct ProcessingCore::Impl {
    Chunker chunker{ChunkingPolicy{kMaxChunkTokens, kChunkOverlap,
                                   kParagraphPreferenceWindow}};
    RetrievalEngine engine;
    ContextBuilder context;
    Corpus corpus;
};

namespace {

// a moved-from core has no impl, reads on it act like an empty corpus
// instead of crashing. template so it never has to name the private Impl
template <typename T>
const T& state_or_empty(const std::unique_ptr<T>& impl) {
    static const T empty;
    return impl ? *impl : empty;
}

// public term arguments go through the same normalization as everything
// else and have to come out as exactly one token
bool single_term(const std::string& term, std::string& out) {
    std::vector<std::string> terms = TextProcessor::terms(term);
    if (terms.empty()) {
        return false;
    }
    if (terms.size() > 1) {
        throw std::invalid_argument("term must normalize to one token: " + term);
    }
    out = std::move(terms[0]);
    return true;
}

}  // namespace

ProcessingCore::ProcessingCore() : impl_(std::make_unique<Impl>()) { }

ProcessingCore::~ProcessingCore() = default;

ProcessingCore::ProcessingCore(ProcessingCore&&) noexcept = default;

ProcessingCore& ProcessingCore::operator=(ProcessingCore&&) noexcept = default;

std::string ProcessingCore::normalize(const std::string& text) {
    return TextProcessor::normalize(text);
}

void ProcessingCore::rebuild(const Workspace& workspace) {
    if (!impl_) {
        impl_ = std::make_unique<Impl>();
    }

    // checked up front on document ids, not chunk ids, because a duplicate
    // whose text is empty makes no chunks and would slip past the index
    std::unordered_set<std::string> seen;
    for (const Document& doc : workspace.documents()) {
        if (!seen.insert(doc.id()).second) {
            throw std::invalid_argument("duplicate document id: " + doc.id());
        }
    }

    // build the whole new corpus off to the side. if anything throws in
    // here the current corpus hasnt been touched
    Corpus next;
    const std::vector<Document>& docs = workspace.documents();
    for (std::size_t order = 0; order < docs.size(); ++order) {
        std::vector<Chunk> doc_chunks = impl_->chunker.chunk(docs[order], order);
        for (Chunk& c : doc_chunks) {
            next.chunks.push_back(std::move(c));
        }
    }
    next.index.build(next.chunks);

    // commit, swap cant throw so theres no half replaced state
    std::swap(impl_->corpus, next);
}

const std::vector<Chunk>& ProcessingCore::chunks() const noexcept {
    return state_or_empty(impl_).corpus.chunks;
}

std::size_t ProcessingCore::chunk_count() const noexcept {
    return state_or_empty(impl_).corpus.chunks.size();
}

std::size_t ProcessingCore::document_frequency(const std::string& term) const {
    std::string normalized;
    if (!single_term(term, normalized)) {
        return 0;
    }
    return state_or_empty(impl_).corpus.index.document_frequency(normalized);
}

std::size_t ProcessingCore::term_frequency(const std::string& term,
                                           const std::string& chunk_id) const {
    std::string normalized;
    if (!single_term(term, normalized)) {
        return 0;
    }
    return state_or_empty(impl_).corpus.index.term_frequency(normalized, chunk_id);
}

std::vector<SearchResult> ProcessingCore::search(const std::string& query, int k) const {
    const Impl& state = state_or_empty(impl_);
    return state.engine.search(query, k, state.corpus.chunks, state.corpus.index);
}

// same ranking as search, the builder just trims it to the budget
std::vector<ContextItem> ProcessingCore::build_context(const std::string& query,
                                                       int k,
                                                       std::size_t token_budget) const {
    std::vector<SearchResult> ranked = search(query, k);
    return state_or_empty(impl_).context.build(ranked, token_budget);
}

}  // namespace aiws