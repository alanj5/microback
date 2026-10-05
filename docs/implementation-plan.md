# microback implementation plan

## Authority and inspection (2026-10-05)

The complete 12-page [technical specification](specification.pdf) is the architecture,
semantics and deliverables authority. [The development request](development-request.md)
supplies the stage gates and Git workflow. This plan reconciles both before implementation.
The initial workspace was empty: no code, build system, tests, history or remote.
A new `microback` repository is used; no existing design needs preservation.
Available: Apple Clang 17.0.0, Apple Git 2.50.1, Python 3.14.5, arm64 macOS.
CMake 4.4.4, Ninja 1.13.2, clang-format 23.1.2 and PyYAML 6.0.3 were installed
in the sibling `.microback-tools` environment. System `g++` is Apple Clang, not GCC.
GCC and Linux perf validation require Linux; CI will cover GCC/Clang and sanitizers.
There is no configured remote, so completed stages can be committed locally but not pushed.

## Gates applying to every stage

Before starting: inspect status, log and remotes; previous work must be committed.
Work only on the current stage. At the end: format/static checks, Debug build/test,
Release build/test, ASan/UBSan where applicable, stage-specific validation, diff
review, generated-file exclusion, documentation, focused commit and push when possible.
Record exact commands, outcomes, SHA, push outcome, decisions and deferrals in
`docs/checkpoints.md`. A failed acceptance check prevents advancement.
No placeholder APIs, speculative later-stage implementations, fake benchmark results,
raw market datasets, secrets, force pushes or rewritten history.

Single instrument/venue, sequential deterministic processing; integer ticks/lots/ns/IDs
in the hot path. Keep venue parsing outside the core. Strategies get read-only state.
Optimisation requires a measured baseline and profiler evidence. Concurrency is optional
across independent experiments only. L3 exact queue mode is optional and must only be
claimed if supported by the chosen dataset and fully tested.

## Stage 1 — Foundation and domain types

**Dependency:** inspection and this checked plan. **Status:** complete locally; Linux CI unexecuted.

**Objective:** a clean C++20 project that builds and tests without simulation.

**Deliverables:** CMake `microback_core` and `microback_tests`, include/src/apps/tests/
benchmarks/python/scripts/examples/docs structure as files become useful; strong wrappers
for signed 64-bit PriceTicks, QuantityLots, Timestamp and unsigned 64-bit OrderId,
Sequence; compact Side, MarketEventType, OrderType, TimeInForce, OrderStatus enums;
Catch2 tests; warnings, ASan/UBSan options; formatting/static configuration; Ubuntu
GCC/Clang/style/sanitizer CI; README build commands and ignore rules.

**Non-goals:** book, events/replay, matching, strategies, portfolio, adapters, bindings.
The specification's skeleton synthetic events move to Stage 3 to respect the request's
Stage 1 boundary. Wrappers preserve the specification's integer widths, rather than its
illustrative aliases, to satisfy explicitly requested strong typing.

**Tests/acceptance:** explicit construction, no implicit conversion or cross-domain
arithmetic, comparisons, integer-width/range checks, enum widths/values; clean configure,
warning-free Debug/Release builds and passing CTest; passing sanitizer tests; YAML parses;
format check passes. GCC validation occurs in CI and must not be reported as run locally.

**Commit:** `build: initialise C++20 project and core domain types`.

## Stage 2 — Price-time-priority book

**Dependency:** Stage 1. **Status:** complete locally; Linux CI unexecuted.

**Objective:** correct independently usable order-level book and derived state.

**Deliverables:** Order/AddOrder/ModifyOrder, PriceLevel, indexed OrderLocation,
OrderBook with descending bid/ascending ask maps and stable FIFO lists; add/cancel/
modify, clear, best prices, spread, mid, microprice, top-N imbalance, depth and VWAP;
invariant validator and immutable inspection. Queue priority resets for price changes
and quantity increases; reductions retain priority. Define empty-book, rounding,
invalid-ID, quantity and overflow policies. Preserve half-tick reporting precision.

**Non-goals:** replay, matching client orders, custom ladders or optimisation.

**Tests/acceptance:** add both sides, multiple levels, best prices, FIFO; cancel first/
middle/last, unknown IDs and empty-level deletion; modify price/increase/decrease;
depth/spread/mid/microprice/imbalance/VWAP (including insufficient liquidity); totals
equal constituent quantities, no negatives, correct ordering, crossed-book detection;
indexed cancellation without full scans; Debug/Release/ASan/UBSan pass.

**Commit:** `feat: implement price-time-priority limit order book`.

## Stage 3 — Canonical events and deterministic replay

**Dependency:** Stage 2. **Status:** complete locally; Linux CI unexecuted.

**Objective:** reconstruct known book states from validated fixtures.

**Deliverables:** MarketEvent with specification fields, EventSource, in-memory and
CSV sources, deterministic replay engine and `microback_cli`; snapshot/reset protocol,
incremental add/modify/cancel/trade handling, timestamp/sequence/gap validation, strict
malformed-data policy and replay counters. Document L2 aggregate IDs vs L3 order IDs
and snapshot grouping explicitly. Input order is lexicographic (timestamp, sequence);
fail fast on regressions rather than silently sorting malformed streams.

**Non-goals:** exchange-specific formats, client execution, binary IO, strategies.

**Tests/acceptance:** snapshot, snapshot then incrementals, repeated snapshots/reset,
add/cancel/modify/trade, equal-time sequence ordering, timestamp/sequence regression,
gaps, malformed CSV/event, incremental-before-snapshot and crossed-book errors;
fixture final state known exactly; repeated replay outputs byte-identical; all gates pass.

**Commit:** `feat: add deterministic market event replay`.

## Stage 4 — Latency-aware execution

**Dependency:** Stage 3. **Status:** complete locally; Linux CI unexecuted.

**Objective:** hypothetical client orders execute only at exchange arrival.

**Deliverables:** SimOrder/Fill and lifecycle; Market/Limit, GTC/IOC, crossings and
passive placement, multi-level book walking/partial fills; submit/cancel scheduler;
ConstantLatency interface; explicit same-time internal/external tie policy;
maker/taker classification and exact configured fee representation; L2 default
conservative, proportional neutral and optimistic queue models. Record decision and
arrival mids and actual execution price. Historical reconstructed state and simulated
consumption must have a documented separation/market-impact assumption. Preserve
specification event cycle: external update, derived state, due exchange effects,
strategy observation, accept requests, schedule arrival, execute only when due.

**Non-goals:** portfolio, reference strategies, stochastic latency, L3 claim without data.

**Tests/acceptance:** one/multi-level market walks, insufficient depth, partial fills,
crossing/passive limits, IOC residual cancel, GTC persistence, cancel latency and
fill/cancel race, all lifecycle states, maker/taker fee/rebate arithmetic; initial
queue ahead, execution depletion and each cancellation model; zero/nonzero latency,
same-time ordering and market change between decision/arrival. An adversarial stale-book
test must fail a look-ahead implementation. Filled+remaining+cancelled quantity reconciles.

**Commit:** `feat: implement latency-aware execution simulation`.

## Stage 5 — Portfolio and risk

**Dependency:** Stage 4. **Status:** complete locally; Linux CI unexecuted.

**Objective:** exact financial state and controlled exposure.

**Deliverables:** cash, inventory, average entry, realised/unrealised/gross/net PnL,
fees/rebates, midpoint marking, turnover and gross exposure; max position, order size,
notional limits on submission, open-order exposure reservation and rejection records;
decision/arrival slippage accounting. Document tick/lot monetary scale, fee rounding,
overflow and reversals. Reporting doubles must not become authoritative prices/quantities.

**Non-goals:** strategies, Python/research analytics.

**Tests/acceptance:** hand-calculated profitable/losing round trips, partial fills and
positions, many fills, short and reversal accounting, maker/taker/rebate, realised/
unrealised/net PnL, cash/equity reconciliation; each risk rejection, pending exposure,
cancel releases reservation, rejected orders leave financial state intact; exact results.

**Commit:** `feat: add portfolio accounting and risk controls`.

## Stage 6 — Strategy framework

**Dependency:** Stage 5. **Status:** complete locally; Linux CI unexecuted.

**Objective:** deterministic native strategies through restricted engine APIs.

**Deliverables:** Strategy start/event/fill/timer/end hooks, Context read-only market/
portfolio access and controlled submissions/cancellations; configurable imbalance
strategy (default levels 5, entry 0.35, flatten 0.10) and inventory-skewed maker with
spread limits, inventory skew, cancel/replace and sizing; results with orders/fills/
equity/positions/market history for future research and bindings.

**Non-goals:** profitable-strategy claims, Python callbacks, future labels visible in Context.

**Tests/acceptance:** hooks and fill callbacks/order ordering, compile-time immutable
book access, Context submissions and positions, deterministic imbalance actions,
inventory-sensitive quotes, replacement races, timers and causality; both end-to-end,
two identical full runs produce identical results. Document execution assumptions.

**Commit:** `feat: add strategy framework and reference strategies`.

## Stage 7 — Python research API

**Dependency:** Stage 6. **Status:** complete locally; Linux CI unexecuted.

**Objective:** installable Python-driven complete native backtests.

**Deliverables:** pybind11 extension `microback._core` (CMake target `_microback`
with output name `_core`), scikit-build-core pyproject, public run/config/result API;
fills/orders/equity/positions DataFrames and metrics, plotting, Experiment parameter
grid, walk-forward with past-only selection; reproducible examples, Python tests and
package/install CI. Native C++ strategies remain core benchmark path.

**Non-goals:** per-event Python strategy callbacks (optional), real-data ingestion here.

**Tests/acceptance:** wheel/editable build, clean-environment install/import, C++ result
equivalence, dtype/schema conversions, seeded reproducibility, complete parameter grid,
walk-forward frozen parameters and no test-window tuning or future leakage; runnable
research example without C++ edits. No future-fill labels in strategy-visible state.

**Commit:** `feat: expose simulation engine through Python`.

## Stage 8 — Data, binary IO and measured performance

**Dependency:** Stage 7. **Status:** planned; split into focused gated sub-stages.

The specification places real data before Python and research before performance;
the request explicitly groups data/performance here and research last. This preserves
all deliverables while using synthetic fixtures for earlier Python verification.

### 8a — Venue data and binary replay

**Status:** complete locally; real capture retained outside Git.

**Deliverables:** one documented liquid crypto venue adapter/collector (Kraken-style
L2 target), real captured dataset outside Git with provenance/checksum/license notes,
exact decimal-to-tick/lot conversion, validation, deterministic generator with small,
1M/10M/100M stream modes; BinaryEventSource and `microback-convert input.csv output.mkt`.
Specify little-endian 48-byte event records, timestamp range, type/side/flags/symbol ID
validation, framing/version and truncation detection; never serialize native padding.

**Non-goals:** multi-venue, live trading, optional exact L3 until source supports it.

**Tests/acceptance:** captured venue fixtures, snapshot/incremental conversion, decimal
alignment rejection, sequence gaps/reset, binary byte-level/round-trip/truncated/invalid
record cases, CSV/binary identical results; real-market replay reproducible from collection
instructions; large generated stream validates without committing it.

**Commit:** `feat: add venue data adapter and binary market replay`.

### 8b — Benchmark baseline and profiling

**Status:** complete locally with macOS sample; Linux counters unavailable.

**Deliverables:** Google Benchmark add/cancel/modify/best prices/trade/book walking/
1M and 10M replay; CSV vs binary; benchmark smoke CI; profile.sh for Linux perf stat/
record/report; retain repeated run outputs with CPU/RAM/OS/compiler/version/flags/
build/commit, events/sec and ns/event. On macOS use legitimate available profiler and
record unavailable Linux counters explicitly; Linux perf evidence remains outstanding
until run on Linux. Benchmark synthetic generation separately from event application.

**Non-goals:** fabricated results, timing assertions in CI, speculative optimisation.

**Tests/acceptance:** benchmark execution and reproducible repetitions, machine metadata,
profile evidence with cycles/instructions/IPC/cache references/misses/branches/misses
where supported; consistent workload and correctness across compared replay formats.

**Commit:** `perf: establish reproducible replay benchmarks and profiles`.

### 8c — One justified optimisation

**Deliverables:** identify measured bottleneck, one focused implementation change,
before/after benchmark report and percentage change with uncertainty/trade-offs.
Alternative price ladder is optional only after evidence; independent-run concurrency
is optional (TSan required if nontrivial shared batch code is introduced).

**Tests/acceptance:** profiler motivates change, identical semantic/regression results,
full gates pass, repeated comparable measurements demonstrate actual improvement.
If no justified improvement is found this acceptance remains open, not claimed complete.

**Commit:** `perf: benchmark and optimise market replay engine`.

## Final stage — Research, documentation and review

**Dependency:** all previous applicable acceptance criteria pass. **Status:** planned.

**Objective:** reproducible execution research and defensible systems evidence.

**Deliverables:** latency grid 0/50/100/250/500/1000 us; optimistic/neutral/conservative
queue comparison; frictionless/fees/execution/latency ablations; maker spread capture,
fill probability, inventory and post-fill adverse selection at 1/10/100/1000 ms where
data resolution supports it. Future mids computed only after simulation. Walk-forward
past-only selection. Record actual fee schedule or explicitly labelled scenario fees.
Metrics: gross/net PnL, fees, drawdown, fill ratio, maker/taker fraction, spread capture,
decision/arrival slippage, mean/max position/exposure/turnover, adverse selection,
Sharpe/Sortino on fixed-time returns with documented annualisation/undefined cases.

Required useful plots: equity, inventory, spread, imbalance, fills vs midpoint,
gross/fees/slippage/net, latency, queue models, adverse selection and benchmark throughput.
Do not double-subtract slippage already included in realised execution PnL.
Save configs/seeds/data checksums/tables/plots/scripts and one-command demonstration.

Finish README with purpose, architecture/features, example, assumptions, measured
benchmarks/research, build/install, reproducibility and limitations; write
`docs/architecture.md`, `execution-model.md`, `data-format.md`, `benchmarking.md`,
`assumptions.md`, research report and reproduction commands. Use request's hyphenated
filenames in place of specification's illustrative underscored names.

**Non-goals:** claims of profitability/true L2 queue precision, web/cloud infrastructure,
unmeasured CV numbers, unsupported horizon labels.

**Tests/acceptance:** regenerate research and plots from frozen data/config; inspect
plots, no tuning leakage; reproducible build/test/benchmark/demo; repository-wide review
for market semantics, look-ahead, float misuse, stale TODOs/dead code/placeholders,
lifetimes/ownership, hot-path allocations/copies, API boundaries and errors; fix substantive
issues and rerun full C++/Python/package/sanitizer checks. Mark complete only when
every applicable request Definition-of-Done item and spec §§17–19 is satisfied.

**Commits:** `docs: publish reproducible execution research and project documentation`,
then `fix: address final correctness and reproducibility review` if necessary.

## Specification coverage audit

| Specification | Planned stages |
| --- | --- |
| §§1–3 scope, boundaries, integer types/records | 1, 3–7 |
| §§4–5 FIFO book, depth, VWAP, half-tick mid and microprice | 2 |
| §6 ordering, event cycle, determinism | 3, 4, 6; regression tests throughout |
| §7 latency, walking, queue models, optional L3 | 4, 8a |
| §8 fees, decision/arrival slippage, adverse selection, accounting/risk | 4, 5, final |
| §9 strategy hooks and both reference strategies | 6 |
| §10 canonical sources, real/synthetic data, binary format, validation | 3, 8a |
| §11 bindings, research outputs and grids | 7 |
| §12 walk-forward, metrics and execution studies | 7, final |
| §13 benchmarks, profiling, justified optimisation, optional batch parallelism | 8b–8c |
| §14 tests, invariants and sanitizer coverage | every gate; 1 CI, 2–7 tests |
| §15 structure, build targets, CI and package | 1 foundation; 3 CLI; 7 package; 8 benchmarks |
| §§16–19 deliverables, final evidence and guardrails | final audit; entire plan |

All optional features (stochastic latency, Python callbacks, exact L3, second backend,
parallel batches and parser fuzzing) remain explicitly optional. No v1 requirement is removed.
