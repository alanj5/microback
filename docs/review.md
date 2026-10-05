# Final review and evidence

Review scope: authoritative specification §§1–19 and development-request Definition
of Done, including correctness, causality, ownership, cost accounting, reporting,
performance, source/package boundaries, error paths, docs and generated-data exclusion.

## Substantive fixes

1. Portfolio subtraction previously negated signed 128-bit fee/initial-cash values.
   At the signed minimum this is undefined. Checked subtraction now handles exact
   representable differences and rejects true overflow transactionally. Negative
   marks are rejected; extreme native/Python cases test exact minimum handling.
2. A MarketMaker reused across independent runs retained IDs from the previous engine.
   on_start resets IDs/cancel flags; repeat-run equality now has a native regression.
3. ExecutionEngine::finish previously checked only replay failures. It now preserves
   its own sticky failure and marks completion validation failures sticky as well.
4. Max drawdown/position/exposure must include between-grid events. Risk maxima use
   the full observed history; fixed-time sampling is reserved for returns/means.
5. Python build cache is separated by wheel tag, and CMake explicitly finds Python
   Interpreter/Development.Module. This avoids reusing another Python ABI cache.
6. Window splitting skips empty time partitions and generates a snapshot from strictly
   preceding factual state. Replay and disjoint chronology tests cover the splitter.
7. Benchmark reproduction reads the actual cached compiler rather than assuming one,
   records RAM on Linux/macOS and rejects missing/error benchmark measurements.

The three native fixes retain normal fixture/strategy results; whole-result equality
against a preserved pre-optimization extension was established for 24 configurations
after the native boundary corrections as well. Strong types prevent hot-path floating prices/lots/times;
reporting mids, signal ratios and post-hoc analytics use floats only at their boundary.

## Ownership and event ordering

Book locations use stable list/map nodes with copy/move disabled. Execution orders
live in stable maps. Fill callbacks observe updated order and portfolio state. The
runner owns engine/Context/strategy callback scope; no borrowed lifetime escapes.
Equal-time external/action/timer order is explicit and adversarial stale-book tests
exercise arrival after price changes. Source timestamp/sequence regressions fail
rather than being sorted. No future labels are strategy-visible; walk-forward selects
only preceding files and freezes the winner before each test.

Historical/simulated liquidity separation, same-price passive-print matching, L2
queue uncertainty, packet/cross-channel receipt-order limits, cash/margin assumptions,
scenario fees and end residuals are documented rather than hidden as exact venue
behaviour. Slippage is not double-subtracted. Future markouts use end/staleness
censoring and are reporting-only. Extremely annualized short-window ratios are not
presented as forecast quality.

## Required deliverables

| Deliverable | Evidence |
| --- | --- |
| C++20 FIFO book and deterministic replay | 52 native cases, validated CLI, Debug/Release/sanitizers |
| Orders, partial fills, GTC/IOC, latency, fees, walking, L2 queue | execution tests and execution-model.md |
| Portfolio, risk and both native strategies | exact ledger/risk/strategy tests, repeat-run equality |
| Real data and deterministic large generator | checked Kraken manifest; 1M/10M replay; 100M mode available |
| Binary source/converter | exact byte, malformed/truncation and full CSV/binary equality tests |
| No look-ahead and reproducibility | stale-book, hook/timer, future-window-change and whole-result tests |
| Python package, DataFrames, grids, walk-forward | clean wheel/editable installs, Python tests, runnable example |
| Benchmarks and justified optimization | five repeated raw runs, before/after samples, 94.8% workload improvement |
| Latency/queue/cost/passive research and useful plots | docs/research manifests, tables, plots; exact local regeneration |
| CI and reproducible build/demo/docs | workflows, demo.sh, architecture/execution/data/assumptions/build guides |

Optional exact L3 client queue, stochastic latency, Python callbacks, custom ladder,
concurrency and parser fuzzing are not claimed. No TSan need is introduced by the
single-threaded implementation. Linux perf and GCC/Clang CI remain unexecuted on
this macOS host; legitimate macOS stack sampling supplies profiler evidence. No
configured Git remote exists, so commits remain local and CI cannot be dispatched.

No stale TODO/FIXME/placeholder functions, secrets, raw large datasets or build
artifacts are intentionally tracked. Reporting result/history copies remain a known
cost of research runs. The one measured hot-path change removed depth materialization
for quantity lookups; further changes require new profiles, not speculative tuning.
See checkpoints.md for exact executed commands and outcomes.

Post-hoc review also ensures missing marks yield None (never nonstandard JSON NaN),
quantity totals use Python integers instead of overflowing NumPy int64 reductions,
and drawdown differences use exact monetary atoms before reporting conversion. The
passive markout study filters maker fills; marketable quote arrivals remain classified
and reported as taker fills, including their actual scenario taker fees.

Converter review found a concrete filename collision: fixed `output.mkt.tmp` could
also be the CSV input name and be truncated. Conversion now reserves an exclusive
temporary directory in the output's filesystem, then renames only validated data.
CTest verifies source preservation, same-input rejection, retention of an existing
output on malformed data, and cleanup. Kraken normalization also rejects float/bool
inputs before Decimal construction and replaces equal price keys to preserve the
latest lexeme/exponent for CRC32; dedicated parser regressions cover both cases.

Benchmark counters now name seconds per item in the raw Google Benchmark output;
benchmark.py derives ns/event with an explicit 1e9 conversion (CV stays dimensionless).
Historical raw baseline/optimized files remain unchanged and their older counter
unit convention is explained in benchmarking.md. Capture provenance distinguishes
conversion completion from the canonical first/last receipt timestamps. Position
absolute-value reporting uses Python integers, covering the signed int64 minimum
without NumPy abs wrapping; a regression checks both maximum and mean magnitude.
