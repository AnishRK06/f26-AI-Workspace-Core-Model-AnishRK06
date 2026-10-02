# M2 Technical & Design Understanding

This file is an assessed technical-understanding artifact, not ordinary project documentation.
Answer all four questions using your own submitted implementation. Concise answers are acceptable when they are technically correct and specific.

Generic descriptions of C++ concepts or restatements of the assignment that do not identify and explain corresponding parts of your code will receive limited credit.

## 1. Polymorphism and dynamic dispatch - 1.5 points

Identify one place in your M2 implementation where runtime polymorphism occurs. Name the relevant base interface, derived implementation, and `ProcessingCore` function involved. Trace the call from `ProcessingCore` to the selected strategy implementation and explain why the derived implementation is invoked.

Then explain what would change if the relevant operation were not declared `virtual`.

Where it happens: `ProcessingCore::search` in `src/processing_core.cpp`.
- Base interface: `RetrievalStrategy`
- Derived implementation: `RetrievalEngine` (the default)
- `ProcessingCore` function: `search`

Call trace:
1. The default constructor creates `std::make_unique<RetrievalEngine>()` and stores it in `Impl::retrieval`, which has type `std::unique_ptr<RetrievalStrategy>`.
2. `core.search(query, k)` runs `impl_->retrieval->search(query, k, impl_->chunks, impl_->index)`.
3. `search` is declared `virtual ... = 0` in `RetrievalStrategy`, and `RetrievalEngine::search` is marked `override`. The pointer's type is the base, but the object is a `RetrievalEngine`. The virtual call is resolved at runtime using the object's real type, so `RetrievalEngine::search` runs.

If a different strategy is injected (for example `ReverseRetrieval` in my tests), the same line runs that class's `search` instead. `ProcessingCore` never names the concrete class.

If `search` were not virtual: the call would be decided at compile time from the pointer's type, `RetrievalStrategy`. It would always call the base version and ignore the injected strategy, so runtime substitution would not work. Also, `override` in `RetrievalEngine` would fail to compile, because there would be no virtual function to override.

## 2. Ownership and lifetime - 1.5 points

Identify where one of the strategy objects is created, where ownership is transferred, and which object ultimately owns it. Explain how `std::unique_ptr` represents that ownership relationship and when the strategy object is destroyed.

Also explain why `ProcessingCore` is move-only and why the strategy base classes require virtual destructors.

Example: the default `RetrievalEngine`.
- Created: in the `ProcessingCore()` default constructor, with `std::make_unique<RetrievalEngine>()`.
- Transferred: that `unique_ptr` is moved into `Impl`'s constructor, which moves it into the `Impl::retrieval` member. For injected strategies, the caller writes `std::move(ptr)` into the configurable constructor. The parameter takes ownership, and the caller's pointer becomes null (checked in `test_core_owns_and_destroys_strategies_once`).
- Final owner: `ProcessingCore` owns `Impl` through `impl_`, and `Impl` owns the strategy through `std::unique_ptr<RetrievalStrategy> retrieval`.
- How `unique_ptr` represents it: `unique_ptr` means exactly one owner. It can't be copied, only moved, and it deletes the object automatically.
- When destroyed: when the `ProcessingCore` is destroyed. Its destructor destroys `impl_`, which destroys the three `unique_ptr`s, which delete the strategies. My test counts this and sees exactly 3 destructions only after the core goes out of scope.

Why `ProcessingCore` is move-only: it holds `unique_ptr`s, which can't be copied. A copy would mean two cores owning the same strategy, which would delete it twice. Copying would also require cloning a polymorphic object whose real type the core doesn't know. So copy is deleted, and a move just transfers `impl_` to the new core.

Why virtual destructors: the core deletes strategies through a base pointer (`unique_ptr<RetrievalStrategy>`). Without a virtual destructor, only the base destructor would run, the derived part would not be cleaned up, and the behavior is undefined. With it, the derived destructor runs first, then the base destructor.


## 3. Architecture, extensibility, and M1 compatibility - 1.5 points

Explain one specific architectural decision in your M2 implementation that makes the processing system extensible while preserving M1 behavior.

Identify the classes or interfaces involved and explain both:
- how the default configuration preserves M1 behavior; and
- how a different implementation can be substituted without changing the normal `ProcessingCore` API.

Include one plausible design alternative and explain why the M2 design is preferable for this milestone. The alternative does not need to be something you actually implemented.

Decision: `ProcessingCore::Impl` stores each processing role as a `std::unique_ptr` to its interface (`ChunkingStrategy`, `RetrievalStrategy`, `ContextStrategy`) instead of a concrete M1 object. `rebuild`, `search`, and `build_context` call through those pointers.

How the default preserves M1: the default constructor installs the same M1 classes M1 used: `Chunker` with the M1 policy (120 / 20 / 20), `RetrievalEngine`, and `ContextBuilder`. Their algorithms were not changed, so the output is the same as M1. My test `test_default_matches_help_session_trace` checks the help-session example, including score `2.962461898616` and truncated context `"search search 42"`.

How a different implementation is substituted: a caller writes a class that derives from one of the interfaces and passes it to the second constructor. `search`, `rebuild`, and `build_context` keep exactly the same signatures, so the caller uses the same API and only the object behind the pointer changes.

Alternative: use an enum (like `RetrievalMode::TfIdf`) and a `switch` inside `ProcessingCore::search`.

Why M2's design is better: with the enum, every new algorithm requires editing `ProcessingCore`, and outside code like instructor tests can't add its own strategy at all. With interfaces, new strategies can be added without touching `ProcessingCore`.

## 4. Testing and defect reasoning - 1.5 points

Select one meaningful test from your `tests/student_tests.cpp`.

Explain:
- what M2 requirement the test validates;
- what specific implementation defect the test could detect; and
- why your test provides useful evidence beyond simply rerunning the supplied public tests.

If your test uses a custom strategy, explain how its observable behavior demonstrates that `ProcessingCore` is actually using runtime substitution.

Test: `test_each_operation_dispatches_to_injected_strategy`

Requirement validated: `ProcessingCore` must actually use the injected strategy for each operation:
- `rebuild` uses the chunking strategy
- `search` uses the retrieval strategy
- `build_context` uses the retrieval strategy and then the context strategy

Defect it can detect: `ProcessingCore` declaring the interfaces but still calling the M1 classes directly, for example `search` doing `RetrievalEngine{}.search(...)` instead of `impl_->retrieval->search(...)`. It also catches arguments like `k` or `token_budget` not being passed through.

How the custom strategies prove substitution: the test injects three custom strategies whose output the M1 code could never produce:
- `MarkerChunker` makes chunk ids ending in `#m`.
- `ReverseRetrieval` returns `c#m` then `b#m` for the query `"alpha"`. The default engine would return `a` then `b`, and document `c` doesn't even contain "alpha".
- `TagContext` returns text like `"ctx:c#m"`.

They also share a `Probe` object that counts calls and records `k` and the budget. If the core called the default classes, both the outputs and the counts would be wrong.

Why it's more useful than the public tests: the public test checks one output per strategy from a single construction. My test also checks how many times each strategy is called and with what arguments. Other tests check that injected strategies keep working after moves and after a failed rebuild.
