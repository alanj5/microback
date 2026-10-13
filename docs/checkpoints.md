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

## Stage 4 — Latency-aware execution simulation (2026-10-05)

Implemented `execution.hpp` / `execution.cpp`, client lifecycle, deterministic arrival
scheduler, ConstantLatency, market/crossing/passive limits, partial fills, GTC/IOC,
latency-aware cancellation, per-fill maker/taker fees/rebates and three L2 queue models.
`docs/execution-model.md` specifies event ties, snapshot continuity, liquidity overlay,
integer fee representation and modelling limits. Decision/arrival mids are retained.

Final validation commands (all passed):

```sh
export PATH=/Users/alanD/Desktop/Projects/.microback-tools/bin:$PATH
cmake --build build/debug --parallel 4
ctest --test-dir build/debug --output-on-failure
cmake --build build/release --parallel 4
ctest --test-dir build/release --output-on-failure
cmake --build build/sanitize-llvm --parallel 4
ctest --test-dir build/sanitize-llvm --output-on-failure
python3 scripts/check_format.py
/opt/homebrew/opt/llvm/bin/clang-tidy -p build/sanitize-llvm src/execution.cpp tests/cpp/test_execution.cpp --quiet
build/debug/microback_tests --reporter compact
git diff --check
```

35 CTest cases (34 Catch2 cases / 1,809 assertions plus fixture CLI integration).
The adversarial latency test changes asks between decision and arrival: a market buy
fills at 110, not stale 102; a limit capped at 102 remains unfilled. Arrival between
external events uses only the old state at its actual timestamp. Trade-at-arrival,
cancellation latency, passive FIFO and repeated full runs are tested. L2 trade prints
cannot supply stale taker liquidity or double-count subsequent depth decreases.

Review corrected two overlay/queue rules before committing: growth must increase
available liquidity only once, and neutral cancellation uses the pool after previously
observed trades. Regression tests cover both. An initial test build had a Catch2 macro
parenthesis error; corrected and rerun. An initial LLVM tidy invocation against Apple's
compilation database failed to resolve standard headers; final analysis used the LLVM
build database. Final compiler/style/static checks had no diagnostics.

Design: historical reconstruction is immutable to clients; separate liquidity overlay
prevents reuse of consumed lots. Passive queues are explicitly approximate, even for
records with historical IDs; no exact L3 mode claim. Snapshot replacement cancels
resting clients rather than inventing continuity. Pending actions beyond final data
remain pending. Signed integer ppm fees are exact in millionths of tick*lot with
128-bit intermediates; per-fill tick*lot notional overflow fails explicitly.

Portfolio/PnL, submission risk limits/rejection records, strategies, bindings, real
venue data, binary replay, profiling/benchmarks and research remain later stages.
Git: Stage 3 is `d1d0d5f`; Stage 4 is the focused execution commit containing this report.
Not pushed: no configured remote. Next: Stage 5 portfolio accounting and risk controls.

## Stage 5 — Accounting and risk

Implemented integer portfolio/basis/PnL, fees, exact midpoint marks, signed slippage,
submission size/position/notional limits, live-order reservations and rejection logs.
Previous stage: `90cd605`. `PATH=/Users/alanD/Desktop/Projects/.microback-tools/bin:/opt/homebrew/opt/llvm/bin:$PATH scripts/validate.sh` passed Debug/Release/ASan+UBSan
CTest (41 cases), formatting, every C++ source's clang-tidy with warnings as errors,
and diff checks. The script records the exact build/test commands. An initial lint
failure in an overflow test was fixed and the whole gate rerun. Long/short round trips,
partial closes, reversals, fees/rebates, exposure reservations and unchanged state on
rejection are covered. Accounting/rounding and notional scope are in accounting-risk.md.
No remote configured, so local commit only. Next: restricted Context and native strategies.

## Stage 6 — Native strategy framework

Previous commit `09bf73a`. Added restricted Context, start/event/fill/timer/end hooks,
configurable imbalance and inventory-skewed maker, deterministic results/history.
The exact gate `PATH=/Users/alanD/Desktop/Projects/.microback-tools/bin:/opt/homebrew/opt/llvm/bin:$PATH scripts/validate.sh` passed all 45 CTest cases in Debug/Release/ASan+UBSan and all
style/static checks. Tests cover immutable book access, callback clock/position, timers,
expected signals, inventory skew and identical complete results. An initial strategy
test expected an impossible flatten fill after trades exhausted depth; the assertion
now checks the actual cancellation and still verifies the generated sell decision.
No venue/Python/future-label functionality was introduced. See strategies.md for tie
semantics and cancellation policy. Local commit only: no remote. Next: Python package.

## Stage 7 — Python research API (2026-10-05)

Implemented pybind11/scikit-build-core package, Config/run/Result, exact money columns,
DataFrames, plotting, Cartesian grids and disjoint past-only walk-forward. Commands
with the tool PATH above: `scripts/validate.sh` (45 cases in each of three modes;
format/static clean), `python -m pip install -e . --no-build-isolation`,
`python -m pytest tests/python -q` (4 passed), `python examples/research.py`,
`clang-tidy -p build/python src/bindings.cpp --quiet --warnings-as-errors='*'`,
`python -m build --wheel --no-isolation`, then wheel install/import/run in the clean
`/tmp/microback-clean` venv: all passed. A binding field typo and adjacent string
arguments flagged by static analysis were fixed before the gate. Native values match
the hand-calculated C++ fixture; huge Python integers survive exactly. Future test
changes do not change the frozen training winner. Binary/real-data support remains
Stage 8. Local focused commit; no configured remote. Next: data and binary replay.

## Stage 8a — Venue data and binary replay (2026-10-05)

Explicit little-endian binary reader/writer, validating converter, source dispatch,
Kraken v2 CRC32/Decimal/depth adapter+collector and streaming small/1M/10M/100M
synthetic generator. `scripts/validate.sh` passed 47 CTest cases in Debug/Release/
ASan+UBSan, format and tidy; dedicated bindings tidy also passed. With the tool PATH,
`pip install -e . --no-build-isolation`; tests were rerun in `/tmp/microback-clean`
after macOS repeatedly marked the hidden tool-venv `.pth` file hidden (Python skips
it). Visible clean venv editable install/import and `python -m pytest tests/python -q`
passed all 7 tests. This environment issue is not a package workaround in source.
`python scripts/collect_kraken.py data/raw/kraken-btc-usd.jsonl --seconds 120` captured
11,862 events; all book checksums verified, no observed trade gaps. `microback-convert`
and `microback_cli` replayed the full capture; Python full-result CSV/binary equality
passed. `scripts/generate.py ... --size 1M` and `--size 10M` plus Release CLI replay
validated 1M/10M events. Tiny captured fixture and exact byte/truncation/invalid binary
cases pass. Source manifests in docs/datasets; raw/generated streams ignored.
100M mode available but not executed. L2/cross-channel receipt-order limitations are
explicit in data-format.md. Local commit only; no remote. Next: measured baseline.

## Stage 8b — Baseline and profiling (2026-10-05)

Google Benchmark covers add/cancel/modify/best/trade/VWAP walk/actual crossing walk,
1M/10M in-memory replay, 1M execution at 10/100 levels and identical CSV/binary file
replay. CMake Release configure/build with MICROBACK_BENCHMARKS=ON passed after
fixing Clang 23 compatibility in third-party __COUNTER__ diagnostics (only dependency
targets) and using nonconst benchmark barrier values. `scripts/validate.sh` passed
47 cases in each of Debug/Release/ASan+UBSan and static/style checks. Dedicated
`clang-tidy -p build/bench benchmarks/bench.cpp --quiet --warnings-as-errors='*'`
passed after strong workload arguments/widening fixes and a documented suppression
of Google's deliberate static registration macro. YAML/style/diff checks passed.
`python scripts/benchmark.py docs/benchmarks/baseline --repetitions 5` and
`scripts/profile.sh docs/benchmarks/baseline/profile` executed successfully. Raw
results, repetitions/CV, machine/flags/compiler/commit/source+data hashes and sampled
stacks are retained. Median execution/1M/100: 1164.27 ns/event; binary/10M: 63.71
ns/event; CSV/10M: 233.70 ns/event. No Linux counters available on macOS; no invented
IPC/cache claims. Profile identifies depth copying/allocations under execution.
No core optimization yet. Local focused commit, no remote. Next: quantity lookup
optimization justified by this baseline/profile, with repeated before/after gates.

## Stage 8c — Measured quantity lookup optimization (2026-10-05)

Baseline committed as 1d8bc93. Replaced visible() full depth copy/scan with const
map aggregate lookup; no ladder, queue or scheduler changes. `scripts/validate.sh`
passed all 48 cases in Debug/Release/ASan+UBSan and style/static checks.
`cmake --build build/bench --parallel 4`, `python scripts/benchmark.py
docs/benchmarks/optimized --filter '^execution/' --repetitions 5`, and
`scripts/profile.sh docs/benchmarks/optimized/profile` all passed. Median ns/event
100-level: 1164.27 -> 60.59 (94.80% less time, 19.22x throughput); 10-level:
463.49 -> 44.28. Individual observations/CV and before/after sample stacks retained.
Optimized profile replaces depth allocation paths with map lookup. This is native
execution-update evidence, not Python end-to-end or plain replay improvement.
`/tmp/microback-clean/bin/pip install -e . --no-build-isolation`, Python tests
(7 passed), bindings tidy, and `scripts/compare_native.py` against the preserved
Stage 8a tool-venv extension on small synthetic/full real CSV passed 24 complete
result comparisons (two strategies * three queues * two latencies * two datasets).
No Linux counters or cross-platform speed claims. Local commit; no remote.
Next: final execution research, reproducibility documentation and review.

## Final stage — Research, documentation and review (2026-10-05)

Implemented post-simulation metrics/markouts, exact-atom drawdown/return differences,
full-event risk maxima, latency/queue/maker grids, explicit same-fill cost ablations,
past-only walk-forward, deterministic plots/manifests and one-command demo. Published
research tables/plots exclude raw capture and derived window CSVs. Architecture,
execution/data/API/assumptions/review and public README now describe all contracts.
The review's signed-minimum subtraction, strategy reuse, sticky finish, converter
filename collision, decimal lexeme/float rejection, Python ABI cache and reporting
integer-overflow issues have regression coverage. Native corrections preserve all
24 normal complete-result comparisons with the pre-optimization extension.

Executed from the repository root (all final checks passed):

```sh
export PATH=/Users/alanD/Desktop/Projects/.microback-tools/bin:/opt/homebrew/opt/llvm/bin:$PATH
bash scripts/validate.sh
cmake -S . -B build/binding-check -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=OFF -DMICROBACK_PYTHON=ON -Dpybind11_DIR=$(/tmp/microback-clean/bin/python -m pybind11 --cmakedir) -DPython_EXECUTABLE=/tmp/microback-clean/bin/python
cmake --build build/binding-check --parallel 4
clang-tidy -p build/binding-check src/bindings.cpp --quiet --warnings-as-errors='*'
cmake --build build/bench --parallel 4
clang-tidy -p build/bench benchmarks/bench.cpp --quiet --warnings-as-errors='*'
python scripts/benchmark.py output/final-benchmark-smoke --filter '^best$' --repetitions 2
python -m build --wheel --no-isolation
/tmp/microback-final-install/bin/pip install --force-reinstall --no-deps dist/*.whl
/tmp/microback-final-install/bin/python -m pytest tests/python -q
/tmp/microback-final-install/bin/python scripts/research.py --output output/research
/tmp/microback-final-install/bin/python scripts/research.py --output output/research-repeat
bash scripts/demo.sh
python scripts/check_format.py
git diff --check
```

Debug, Release and LLVM 23 ASan+UBSan each passed all **52 CTest cases** (50
Catch2 cases plus CLI fixture/converter tests); project and binding/benchmark
static checks passed. **13 Python tests** passed in a clean wheel environment;
package build/install/import, synthetic demo and benchmark smoke passed. An isolated
wheel build also passed during final-stage development. The dedicated binding
compile database avoids temporary pybind11 include paths deleted after isolated
package builds. The visible `/tmp/microback-clean` editable environment avoids
macOS hidden `.pth` handling in the dot-named tools venv; no source workaround.

Complete result equivalence command (24 cases passed after native fixes):

```sh
/tmp/microback-clean/bin/python scripts/compare_native.py /Users/alanD/Desktop/Projects/.microback-tools/lib/python3.14/site-packages/microback/_core.cpython-314-darwin.so data/generated/small.csv data/raw/kraken-btc-usd.csv
```

For every file under output/research, comparing read_bytes() against the same
relative file under output/research-repeat established **53 exact byte equalities**
(CSV/JSON/PNG, including eight generated window CSVs). The published 45 artifacts
omit those window files. All real plot layouts were visually inspected, including
the final maker-only adverse-selection plot. Manifests pin configurations, versions,
source/data/window hashes and seed; dirty=true/parent b002d92 records measurements
made before the final commit. Source hashes match the final measured implementation.
JSON manifests reject NaN; all workflow jobs parse; generated/raw/build environments
are ignored. Repository review and local doc-link checks passed.

Research interpretation: scenario fees overwhelm the small imbalance gross profit;
queue generosity increases fills and losses in this short receipt-ordered capture.
Latency results are path/packet-normalization sensitivity, not a claim that delay
improves execution. The constant-price control has identical latency outcomes and a
degenerate single-fill maker case. Future labels are reporting-only and censored;
walk-forward uses preceding windows with a fresh portfolio. No profits, exact L2
queues or annual forecast quality are claimed. See research/report.md.

Git: final focused commit `docs: publish reproducible execution research and project
documentation` includes reviewed corrections and their tests so the complete final
stage is committed in a working state. No push or hosted CI dispatch is possible:
`git remote -v` is empty. Linux perf/GCC CI remains unexecuted on macOS; macOS
profile evidence and LLVM sanitizers are genuine substitutes. 100M generation is
available but not executed. All optional features remain explicitly optional.
Next stage: none; the locally applicable plan is complete.

## Local commit index

| Checkpoint | Commit |
| --- | --- |
| Checked plan | 2620fee |
| Stage 1 | 4063893 |
| Stage 2 | 8874eb3 |
| Stage 3 | d1d0d5f |
| Stage 4 | 90cd605 |
| Stage 5 | 09bf73a |
| Stage 6 | 6e4a165 |
| Stage 7 | 3cbbb5a |
| Stage 8a | e0f75fd |
| Stage 8b | 1d8bc93 |
| Stage 8c | b002d92 |
| Final | Commit titled `docs: publish reproducible execution research and project documentation` |

All commits are local; no configured remote. The final SHA is available in git log
and the completion response; this table avoids a self-referential commit hash.

## GCC 13 optimized-build portability fix (2026-10-13)

The first hosted run after origin was configured failed in GCC Release, Python
wheel and benchmark-smoke, all at the same execution.cpp diagnostic:
-Werror=maybe-uninitialized for the previous-order optional's side. Other matrix
jobs were cancelled by fail-fast; hosted sanitizer/style jobs passed. The source
optional was guarded before dereference, but GCC's optimized analysis reported the
conditionally constructed payload. GCC 13.4 locally reproduced the exact diagnostic
with the same -O3/-DNDEBUG/project warning flags.

Changed the previous-order capture to an explicitly empty std::optional<Order>,
then assignment only for nonzero historical IDs. Capture still precedes replay;
ID-zero prints/markers still skip lookup, and missing IDs remain empty. Historical
queue/cancellation/consumption logic is unchanged. No warning suppression, reduced
optimization or relaxed -Werror. Existing snapshot, ID-zero trade, modification,
cancellation and replenishment regressions exercise these paths.

Commands executed from the repository root:

```sh
export PATH=/Users/alanD/Desktop/Projects/.microback-tools/bin:/opt/homebrew/opt/llvm/bin:$PATH
/opt/homebrew/opt/gcc@13/bin/g++-13 -O3 -DNDEBUG -std=c++20 -fPIC -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -Werror -Iinclude -c src/execution.cpp -o /tmp/microback-execution-after.o
cmake -S . -B build/gcc13-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/gcc@13/bin/g++-13 -DFETCHCONTENT_SOURCE_DIR_CATCH2="$PWD/build/debug/_deps/catch2-src" -DFETCHCONTENT_SOURCE_DIR_GOOGLEBENCHMARK="$PWD/build/bench/_deps/googlebenchmark-src" -DMICROBACK_BENCHMARKS=ON
cmake --build build/gcc13-release --parallel 4
ctest --test-dir build/gcc13-release --output-on-failure
build/gcc13-release/microback_bench --benchmark_filter='^(add|cancel|modify|best|trade|walk)$' --benchmark_min_time=0.01s
bash scripts/validate.sh
CXX=/opt/homebrew/opt/gcc@13/bin/g++-13 MACOSX_DEPLOYMENT_TARGET=26.0 CMAKE_BUILD_PARALLEL_LEVEL=4 python -m build --wheel --no-isolation -Cbuild-dir='build/python-gcc/{wheel_tag}'
python3 -m venv /tmp/microback-ci-validation
/tmp/microback-ci-validation/bin/python -m pip install dist/*.whl pytest
/tmp/microback-ci-validation/bin/python -m pytest tests/python -q
/tmp/microback-ci-validation/bin/python examples/research.py
/tmp/microback-ci-validation/bin/python scripts/research.py --synthetic-only --output output/ci-fix-research
/tmp/microback-ci-validation/bin/python scripts/compare_native.py /Users/alanD/Desktop/Projects/.microback-tools/lib/python3.14/site-packages/microback/_core.cpython-314-darwin.so data/generated/small.csv data/raw/kraken-btc-usd.csv
git diff --check
```

GCC 13 Release and existing Debug/Release/LLVM ASan+UBSan each passed all 52 CTest
cases; format/static checks passed. GCC benchmark smoke and clean GCC-wheel
installation/import passed, with 13 Python tests, the example and synthetic research.
All 24 complete native results still match the preserved pre-optimization extension.
GCC local validation is macOS arm64, not a substitute for the subsequent Ubuntu
run. The before/after benchmark evidence remains historical and is not re-timed here.

Tooling note: Homebrew poured GCC 13.4 successfully but its older brew client rejected
the configure_gcc_runtime post-install step. Installed compiler/linker executables
were exercised by all builds above. The native build emits a macOS assembler
message about deployment-target overriding; no GCC project warning remains. The
week-old /tmp wheel venv had lost its configuration/pip entry point, so a fresh
visible venv was created rather than installing into the global interpreter.

Git: focused `fix: avoid GCC 13 optional initialization warning` commit; push to
origin/main and its Actions run provide hosted validation. The original failed run
is https://github.com/alanj5/microback/actions/runs/38463448358. Node.js action
runtime deprecation notices are nonfatal and unrelated to this compiler failure.


## GitHub Actions runtime warning cleanup (2026-10-13)

Hosted run 38477067019 for 2e65cbf completed successfully across all eight jobs:
GCC/Clang Debug and Release, sanitizers, Python, formatting and benchmark smoke.
Each job's only check annotation was the Node.js 20 action-runtime deprecation.
Those notices concern CI dependencies, not the C++ or Python execution path.

Updated checkout v4 to v7 and setup-python v5 to v7. Official latest releases are
checkout v7.0.1 and setup-python v7.0.0; both major tags declare node24 in action.yml.
The workflow uses GitHub-hosted Ubuntu runners and ordinary push/pull_request
triggers, compatible with these action versions. Python stays at 3.12 and the
build/test commands and compiler warning settings are unchanged.

Validation: PyYAML parses all five job definitions and verifies six action uses
(five checkout, one setup-python), all at v7; git diff --check passed. The prior
all-green run is https://github.com/alanj5/microback/actions/runs/38477067019.
Focused commit: `ci: update actions to Node.js 24 runtimes`; pushed to origin/main
for full hosted verification of the workflow and warning annotations.

Official references: https://github.com/actions/checkout/releases/tag/v7.0.1 and
https://github.com/actions/setup-python/releases/tag/v7.0.0.
