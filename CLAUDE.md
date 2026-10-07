# CLAUDE.md

## What this project is
A C++ limit order book built as a **learning project** to prepare for a low-latency / quant developer assessment (deadline: Oct 13, 2026). The goal is conceptual fluency in C++, concurrency, memory/latency reasoning, and debugger use, not a polished product. Full requirements live in `SPEC.md`; read it before starting any phase.

## How to work with me (important)
- I am learning. Explain the *why* behind design choices briefly, and prefer small, reviewable steps over large code dumps.
- Don't implement a whole phase in one go. Propose the next small step, wait for me, then build it.
- Prefer letting me write core logic (matching, locking) and then review it. Write scaffolding, tests, and boilerplate freely.
- Keep it simple first. Don't add abstractions, templates, or optimizations before the phase calls for them.
- Do not start a later phase until the current phase's "Done when" checklist in `SPEC.md` is met.
- **Debugger checkpoints are required.** Each phase has LLDB checkpoints in `SPEC.md`. Remind me when I reach one and don't mark the phase done until I've done it.
- Don't change the public `OrderBook` API signatures in `SPEC.md` without asking; later phases depend on them staying stable.

## Stack
- C++20, CMake, clang++ (Apple Silicon macOS)
- Tests: Catch2 (fetched via CMake FetchContent)
- Debugger: LLDB
- Sanitizers: ASan + UBSan (phase 1+), TSan (phase 2+)

## Layout
```
include/ob/        public headers (types.hpp, order_book.hpp, ...)
src/               implementation + CLI main
tests/             Catch2 tests
bench/             phase 3 benchmark harness
docs/              debugging-log.md, perf-log.md
SPEC.md
```

## Commands
```
# Debug build (use for LLDB; -O0 -g)
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug

# Sanitizer builds
cmake -S . -B build/asan -DCMAKE_BUILD_TYPE=Debug -DOB_SANITIZE=address,undefined
cmake -S . -B build/tsan -DCMAKE_BUILD_TYPE=Debug -DOB_SANITIZE=thread

# Release build (benchmarks only)
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release

# Tests
ctest --test-dir build/debug --output-on-failure

# Debug a binary
lldb build/debug/ob_cli
```
`OB_SANITIZE` must be wired up in `CMakeLists.txt` (adds `-fsanitize=$OB_SANITIZE` to compile and link flags).

## Conventions
- Modern C++: RAII, `std::unique_ptr` for ownership, no raw owning pointers, no `using namespace std` in headers.
- Prices are integer ticks (`int64_t`), never floating point.
- Every behavior change gets a test. Run tests before saying something works.
- Benchmarks and timing claims come only from Release builds, and only as relative comparisons (laptop absolute numbers are noisy).
- Log notable bugs and debugger findings in `docs/debugging-log.md`, and benchmark results in `docs/perf-log.md`.
