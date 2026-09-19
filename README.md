# M1 Starter - Processing Core & Retrieval API

Read the M1 specification before coding.

Inside the supplied course container, M1 is mounted at `/mnt`. Build and run the supplied public tests with the provided course configuration:

```bash
cd /mnt
mkdir build
cd build
cmake ..
make
ctest --output-on-failure
```

The starter separates the M1 system into the required processing responsibilities: text processing, chunking, corpus indexing, retrieval/ranking, context construction, and top-level coordination. Complete the implementation in the supplied source files so that these components work together through the provided interfaces.

The provided headers and build configuration are course files and should not be modified. The implementation details inside the source files - including responsibility allocation, state management, helper logic, and interactions among components - are part of the engineering work evaluated in M1.

The supplied CMake configuration builds the instructor-provided `public_tests` executable. You are also expected to create and run your own meaningful tests under `tests/`; you are not expected to reproduce the instructor-only `component_tests` executable from the reference solution.

For submission, include only:

- `src/` - completed implementation source files;
- `tests/` - student-written test files;
- `DESIGN.md` - design documentation.

The included `Document`/`Workspace` files represent the M0 subset required by M1. Preserve the required M0 behavior while extending the workspace with the M1 processing subsystem.
