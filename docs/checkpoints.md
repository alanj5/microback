# Implementation checkpoints

## Stage 1 — Foundation and core types (2026-10-05)

Implemented CMake C++20 configuration, strong 64-bit domains and compact enums,
Catch2 tests, warning policy, ASan/UBSan configuration, style/static analysis,
Ubuntu GCC/Clang CI and README. Empty directories are retained with `.gitkeep`.
The source request was whitespace-normalised without changing its instructions.

Validation (all final commands passed, executed from repository root):

```sh
export PATH=/Users/alanD/Desktop/Projects/.microback-tools/bin:$PATH
cmake -S . -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_SOURCE_DIR_CATCH2="$PWD/build/debug/_deps/catch2-src"
cmake -S . -B build/sanitize-llvm -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ -DMICROBACK_SANITIZERS=ON -DFETCHCONTENT_SOURCE_DIR_CATCH2="$PWD/build/debug/_deps/catch2-src"
cmake --build build/debug --parallel 4
ctest --test-dir build/debug --output-on-failure
cmake --build build/release --parallel 4
ctest --test-dir build/release --output-on-failure
cmake --build build/sanitize-llvm --parallel 4
ctest --test-dir build/sanitize-llvm --output-on-failure
cmake -S . -B build/no-tests -G Ninja -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build/no-tests
python3 scripts/check_format.py
/opt/homebrew/opt/llvm/bin/clang-tidy -p build/sanitize-llvm tests/cpp/test_types.cpp --quiet
/Users/alanD/Desktop/Projects/.microback-tools/bin/python -c 'import yaml; from pathlib import Path; d=yaml.safe_load(Path(".github/workflows/ci.yml").read_text()); assert len(d["jobs"]) == 3'
git diff --check
git check-ignore build/debug/CMakeCache.txt .DS_Store
```

Each configuration passed 2 test cases / 17 assertions. No project compiler or
final clang-tidy diagnostics. Catch2 static registration is excluded from the
throwing-static-initialization check in test configuration only. Catch2 v3.7.1
resolved to `fa43b77429ba76c462b1898d6cd2f2d7a9416b14`.

Initial sanitizer attempt with Apple Clang 17 failed: process sampling showed
a runtime initialization deadlock (`MemoryRangeIsAvailable` -> `get_dyld_hdr` ->
allocator -> `AsanInitFromRtl` -> `StaticSpinMutex`). The stuck process was terminated.
Homebrew LLVM 23.1.2 resolved it, as demonstrated by the successful replacement
build/tests above. `brew install llvm` poured the binaries but its post-install
step reported `unknown install step: configure_clang_system`; the installed
compiler/clang-tidy/runtime were exercised successfully through `/opt/homebrew/opt/llvm`.
See the upstream [Darwin shadow-memory initialization change](https://github.com/llvm/llvm-project/pull/167797).

Design: explicit integer wrappers over specification widths, with validation at
operation boundaries. No book/simulation APIs exist in this stage. GCC and hosted
CI checks were configured but could not be run locally; `/usr/bin/g++` is Apple Clang.

Git: focused foundation commit (identified by this stage's commit in `git log`);
not pushed because `git remote -v` is empty. Repository is healthy locally.
Next: Stage 2 price-time-priority limit order book.

## Stage 2 — Price-time-priority order book (2026-10-05)

Implemented `order_book.hpp` / `order_book.cpp`, deterministic book tests and
`docs/order-book.md`. Chosen baseline: maps, FIFO lists and indexed locations;
quantity increases reset priority, decreases retain it. Price changes splice to
an allocated/validated destination before modifying the original level. Optional
empty metrics, exact half-tick midpoint and documented floored VWAP/microprice.
128-bit integer intermediates protect weighted averages. No matching/replay yet.

Executed final validation, all passed:

```sh
export PATH=/Users/alanD/Desktop/Projects/.microback-tools/bin:$PATH
cmake --build build/debug --parallel 4
ctest --test-dir build/debug --output-on-failure
cmake --build build/release --parallel 4
ctest --test-dir build/release --output-on-failure
cmake --build build/sanitize-llvm --parallel 4
ctest --test-dir build/sanitize-llvm --output-on-failure
python3 scripts/check_format.py
/opt/homebrew/opt/llvm/bin/clang-tidy -p build/sanitize-llvm src/order_book.cpp tests/cpp/test_order_book.cpp --quiet
build/debug/microback_tests --reporter compact
git diff --check
```

9 test cases / 1,604 assertions, including 1,500 seeded invariant checks. Compiler
warnings are errors; final static analysis had no diagnostics. An initial tidy
invocation used a stale compilation database and failed to resolve includes;
rerun after CMake regenerated the database, then corrected optional access diagnostics.
The deterministic seed has a targeted lint exemption. No optimisations or performance
claims. Unknown IDs and invalid/overflowing mutations are covered explicitly.

Git: Stage 1 is `4063893`. Stage 2 is the focused book commit containing this report;
no push possible: no configured remote. Next: canonical events and deterministic replay.

## Stage 3 — Canonical events and deterministic replay (2026-10-05)

Implemented canonical records/validation, owned in-memory and strict CSV EventSource,
OrderBook event application, ReplayEngine, atomic snapshot protocol, replay statistics,
fixture CLI, canonical fixture, integration/parser tests and `docs/data-format.md`.
Strict global sequences and nondecreasing timestamps reject regressions; gaps are
counted. Snapshot replacement retains the previous complete state until commit.
L2 informational trade prints do not also subtract depth. Side denotes resting liquidity.
Failure requires explicit reset. No client simulation/latency/strategies yet.

Executed final validation, all passed:

```sh
export PATH=/Users/alanD/Desktop/Projects/.microback-tools/bin:$PATH
cmake --build build/debug --parallel 4
ctest --test-dir build/debug --output-on-failure
cmake --build build/release --parallel 4
ctest --test-dir build/release --output-on-failure
cmake --build build/sanitize-llvm --parallel 4
ctest --test-dir build/sanitize-llvm --output-on-failure
python3 scripts/check_format.py
/opt/homebrew/opt/llvm/bin/clang-tidy -p build/sanitize-llvm src/event.cpp src/replay_engine.cpp apps/replay.cpp tests/cpp/test_replay.cpp --quiet
build/debug/microback_tests --reporter compact
build/debug/microback_cli tests/fixtures/replay.csv
git diff --check
```

16 CTest cases (15 Catch2 cases / 1,682 assertions plus CLI integration). Three CLI
subprocess runs were compared byte-for-byte against the exact expected output:
`events=9 snapshots=1 sequence_gaps=0`, `bid,100,8`, `ask,102,10`.
CSV and in-memory final states/statistics match; missing-file CLI returns exit 1.
All final static/style checks were clean. Debug/Release use Apple Clang 17; sanitizer
build uses LLVM 23.1.2 as described in Stage 1. Linux CI is still unexecuted.

Design decisions: explicit same-timestamp snapshot transaction with zero-ID markers
because the specification's event record has no snapshot flags; validate complete
snapshot before publication. Keep L2 print and depth-update effects separate to avoid
subtracting a trade twice. Canonical adapters must honour these documented conventions.
Real venue parsing, binary IO, client fills, no-look-ahead execution tests and fees are
intentionally deferred according to the plan. Replay makes no execution realism claim.

Git: Stage 2 is `8874eb3`; Stage 3 is the focused replay commit containing this report.
Not pushed: `git remote -v` is empty. Next: Stage 4 latency-aware client execution,
book walking and conservative/neutral/optimistic L2 queue models.
