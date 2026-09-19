# M1 DESIGN.md

## 1. System structure

The processing core is split into five components plus a facade. Each one owns one step of the flow and only talks to the next one through a small interface.

```
Workspace -> ProcessingCore::rebuild
               -> Chunker (uses TextProcessor)  -> vector<Chunk>
               -> CorpusIndex::build(chunks)    -> postings + id lookup

query -> ProcessingCore::search
           -> RetrievalEngine (uses TextProcessor, reads CorpusIndex + chunks) -> ranked SearchResults
      -> ProcessingCore::build_context
           -> search(...) -> ContextBuilder (uses TextProcessor) -> bounded ContextItems
```

- **TextProcessor** is the only code that decides what a token is. `tokenize` makes one pass over the bytes and returns each token with its byte span in the original text and a paragraph number. `terms`, `normalize` and `join` are all built on top of it. Documents, queries, term arguments and budget counting all go through here, so the normalization rules can't drift between indexing and querying.
- **Chunker** turns one `Document` into ordered `Chunk`s. It never looks at newlines itself; it uses the paragraph numbers from `tokenize` to pick chunk ends, and the token spans for `source_begin`/`source_end`.
- **CorpusIndex** maps each term to a postings list of `(chunk_index, frequency)`, plus a chunk id to position lookup. It answers DF/TF and hands postings to retrieval.
- **RetrievalEngine** scores candidate chunks with the TF-IDF plus coverage formula and returns the top k in a fully deterministic order. It only touches chunks that appear in some query term's postings.
- **ContextBuilder** takes results that are already ranked and fills a token budget, truncating the last item if needed. It knows nothing about scoring or the index.
- **ProcessingCore** coordinates. It owns the state, validates public arguments (term must be one token, duplicate document ids, negative k through the engine) and delegates. None of the algorithms live in it.

## 2. Design decisions

**Ownership.** `ProcessingCore` owns an `Impl` through `unique_ptr` (pimpl from the starter). `Impl` holds the chunker, engine, context builder, and a `Corpus { vector<Chunk> chunks; CorpusIndex index; }`. The chunks vector is the single owner of chunk data. The index does not own chunks; it stores positions into that vector. Retrieval and context building borrow everything by `const&` for the length of one call and never keep references afterward.

**Why positions instead of pointers or copies.** Pointers into the vector would break if the vector reallocated, and copies of chunk text in the index would double memory and could go out of sync. A `size_t` position stays valid when the whole vector is moved or swapped, which is exactly what rebuild does. The cost is that the index is only meaningful next to the vector it was built from, so the two live together in `Corpus` and are always replaced as a pair. `find_chunk` also checks that the chunk at the stored position has the requested id, so being handed the wrong vector returns `nullptr` rather than the wrong chunk.

**Postings.** `unordered_map<string, vector<Posting>>` for average O(1) term lookup. Chunks are indexed in order, so each postings list comes out sorted by `chunk_index` without an extra sort, which lets `term_frequency` binary search with `lower_bound`. Frequencies are counted per chunk in a local map first so each term gets exactly one posting per chunk, which is what makes DF "number of chunks containing the term".

**Retrieval.** Query terms are sorted and deduped before scoring. Deduping is required; sorting is my choice so the floating point sum is always done in the same order and `"a b"` and `"b a"` give bit-identical scores. Scores are rounded to 12 decimals before comparing, so values that differ only by float noise become real ties and fall through to the tie-break (document order, then sequence, then vector position so the order is total and never depends on hash map iteration). `partial_sort` puts only the top k in order, and result strings are copied only for those k.

**Stateless components.** `TextProcessor` is all static functions. `Chunker`, `RetrievalEngine` and `ContextBuilder` hold only configuration (or nothing), so they are safe to call repeatedly and easy to test on their own. All mutable state is in `Corpus`.

## 3. Correctness and consistency

- **One normalization.** Every path that produces or compares tokens calls `TextProcessor`. ASCII checks are done by hand instead of `isalpha`/`tolower`, which depend on locale and are undefined for negative `char`, so high-bit and malformed UTF-8 bytes are always separators.
- **Spans refer to the original text.** They come straight from token byte offsets. Normalizing `text.substr(source_begin, source_end - source_begin)` always gives back the chunk text; tests check this for every chunk.
- **Chunking always moves forward.** The paragraph search never picks a length at or below the overlap (`min_length = max(max - window, overlap + 1)`), so `start = end - overlap` strictly increases even for unusual policies. Invalid policies throw in the constructor.
- **Rebuild replaces, never merges.** A rebuild builds a brand new `Corpus` and swaps it in, so removed documents can't leave stale chunks or postings, and rebuilding the same workspace twice gives the same state instead of doubled state.
- **Failed rebuild changes nothing.** Rebuild works in three phases: validate document ids, build the new corpus in a local, then `std::swap` it in. Anything that throws (duplicate ids, `bad_alloc`) happens before the swap, and a `static_assert` proves the swap itself is `noexcept`. Duplicate ids are checked on document ids rather than chunk ids, because a duplicate whose text is empty makes no chunks and would never reach the index's own duplicate check. `CorpusIndex::build` uses the same build-then-swap pattern on its own.
- **Context stays in budget.** The builder tracks tokens used, adds whole chunks while they fit, adds the largest prefix of the next one, marks it truncated and stops. It skips duplicate chunk ids and never reorders.
- **Moved-from core.** After a move, `impl_` is null. Reads go through a helper that falls back to a static empty `Impl`, so a moved-from core behaves like an empty corpus instead of dereferencing null, and `rebuild` recreates its state.

## 4. Testing strategy

There is one test executable per component plus an end-to-end one. Five of them test a component directly without going through `ProcessingCore`, which keeps failures easy to locate. The course CMake only builds `public_tests`, so mine are built by hand inside the container:

```bash
cd /mnt
for t in text_processor chunker corpus_index retrieval_engine context_builder processing_core; do
  g++ -std=c++17 -Wall -Wextra -Iinclude src/*.cpp tests/${t}_tests.cpp -o t && ./t || echo "FAILED: $t"
done
rm -f t
```

The tests are aimed at mistakes that are easy to make and that the public tests wouldn't catch:

- **text_processor**: separator runs, empty and punctuation-only input, UTF-8 and `0xFF`/`\0` bytes, span accuracy, and paragraph detection for LF, CRLF, spaces/tabs on the blank line, a punctuation-only line (not blank), repeated blank lines (one boundary), and leading blank lines.
- **chunker**: 120/121/240 tokens, paragraph breaks before token 99/100/110/121 (both edges of the window), two breaks in the window (latest wins), CRLF, a single newline (not a break), overlap contents, span round-trips, determinism, and a small 10/2/3 policy that is easy to check by hand.
- **corpus_index**: DF vs occurrence count, TF for missing chunks/terms, postings order, total frequency equals total tokens, rebuild leaving no stale data, repeated builds not duplicating postings, a failed build keeping the old index, and overlap tokens counting toward DF in both chunks.
- **retrieval_engine**: exact expected scores computed from the formula, coverage with 1 vs 2 matched terms, unknown terms still counting in Q, duplicate and reordered query terms, a scrambled chunk vector to prove ties follow document order and sequence rather than insertion or hash order, k limits, and canonical rounding.
- **context_builder**: zero budget, exact fit (not truncated), a budget smaller than the first chunk, stopping after truncation even when a later chunk would fit, attribution carried over, duplicates, and a sweep of every budget from 0 to 12 checking the total never goes over and only the last item is truncated.
- **processing_core** (end to end): a four-document workspace with CRLF, a long document with a paragraph break inside the window, and a punctuation-only document. It checks chunk ids and document order across documents, spans back into the right source, term argument validation, cross-document ranking, context matching search order, rebuild after removing documents, failed rebuilds (including a duplicate with empty text) leaving search results identical, and moved-from behavior.

One of these caught a wrong assumption of mine while writing it: I expected `"retrieval index"` to hit three chunks, but a word at token 110 of the long document is in both `long#0` and the overlap at the start of `long#1`, so there are four. The code was right, and the test now documents that overlap tokens are searchable in both chunks.

## 5. Alternatives considered

**Index stores its own copy of the chunks.** The index would be self-contained and `find_chunk` wouldn't need the vector passed in. I didn't do this because it duplicates every chunk's text, and the core would then have two copies of chunk data that must stay equal. Storing positions keeps one owner, and keeping chunks and index together in `Corpus` removes the sync risk that positions would otherwise introduce.

**Rebuild by clearing and refilling in place.** Simpler to write: clear the vector and the index, then chunk and index each document. The problem is a duplicate id or exception halfway through leaves a partial corpus, which the spec forbids. Checking ids first would handle duplicates but not other exceptions. Build-then-swap handles every failure the same way with no extra cleanup code.

**Full sort instead of partial_sort, and a dense score array instead of a hash map.** A full `std::sort` is simpler but does unneeded work when k is much smaller than the candidate count. A `vector<double>` sized to N for accumulating scores avoids hashing, but costs O(N) memory and an O(N) scan per query even when only a few chunks match. The hash map only grows with the number of real candidates, which fits the fact that most queries match a small part of the corpus.

**Doing paragraph detection in the chunker.** The chunker could scan the raw text for blank lines itself. I put it in `tokenize` instead, as a paragraph number on each token, so newline and CRLF handling exists in exactly one place and the chunker's job is just comparing two numbers.