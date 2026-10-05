You are implementing **microback**, a C++20 market microstructure simulator and backtesting engine with Python bindings.

The repository/workspace contains the full **microback technical specification**. Treat that specification as the **source of truth for architecture, semantics, scope, correctness requirements, terminology and final deliverables**.

Your job is **not** to implement the entire specification in one large change.

Instead, first read the complete specification and then break the implementation into **small, testable vertical stages**. Each stage must leave the repository in a clean, working state with:

- a successful build;
- all relevant tests passing;
- a meaningful working capability;
- no knowingly broken or placeholder functionality;
- documentation updated where appropriate;
- a local Git commit containing only that stage;
- a push to the configured remote if a remote exists and authentication permits it.

Do not proceed to the next stage until the current stage satisfies its acceptance criteria.

# Core development principles

Follow these rules throughout the project:

1. **Correctness before optimisation.**
   Build a simple, correct baseline first. Benchmark and profile before making performance-driven architectural changes.

2. **Do not implement future stages prematurely.**
   Design interfaces so later stages can integrate cleanly, but do not build speculative functionality belonging to later stages.

3. **Preserve deterministic simulation semantics.**
   Given identical:
   - market data;
   - configuration;
   - strategy;
   - random seed;

   a simulation must produce identical results.

4. **Preserve strict causal ordering and prevent look-ahead bias.**
   A strategy must never observe information from the future. Execution latency and event ordering must be modelled explicitly.

5. **Use integer representations in the C++ hot path.**
   Prices, quantities, timestamps and sequence numbers should follow the types and representation described in the specification. Do not use floating point as the authoritative internal representation for prices or quantities.

6. **Do not add concurrency simply to make the project multithreaded.**
   Keep the per-instrument event-processing core deterministic and sequential unless the specification or measured evidence justifies otherwise. Parallelism should initially operate across independent simulations/experiments.

7. **Do not fabricate performance claims.**
   Record actual benchmark results only after running reproducible benchmarks.

8. **Keep exchange-specific parsing outside the simulation core.**
   Convert external data into the canonical internal market-event representation through adapters.

9. **Make assumptions explicit.**
   Especially document:
   - L2 queue-position assumptions;
   - execution semantics;
   - latency;
   - fee calculation;
   - price/quantity conversion;
   - snapshot/reset behaviour;
   - malformed-data handling.

10. **Prefer testable, understandable implementations over unnecessary abstraction.**
    This project must be technically defensible in a C++ / HFT / quant engineering interview.

# Step 0 — inspect and plan

Before modifying implementation code:

1. Read the complete microback specification.
2. Inspect the existing repository.
3. Identify:
   - existing files;
   - current build system;
   - current Git status;
   - existing tests;
   - configured Git remotes;
   - available compiler/tooling versions.
4. Reconcile the specification against the existing repository.
5. Create or update:

`docs/implementation-plan.md`

The plan must contain:

- all implementation stages;
- objectives for each stage;
- exact deliverables;
- explicit non-goals;
- acceptance criteria;
- required tests;
- dependencies on previous stages;
- intended commit message.

Use the stage structure below unless the specification gives a strong reason to refine it.

Do not silently remove requirements from the full specification merely because they are not listed below.

---

# Stage 1 — Repository foundation + core types

## Goal

Produce a professional C++20 project skeleton that builds and tests successfully but contains no substantive market simulation yet.

## Implement

- CMake/C++20 project configuration.
- Repository directory structure from the specification.
- Core strongly typed domain primitives, including:
  - `PriceTicks`
  - `QuantityLots`
  - `Timestamp`
  - `OrderId`
  - `Sequence`
- Core enums such as:
  - `Side`
  - `MarketEventType`
  - `OrderType`
  - `TimeInForce`
  - `OrderStatus`
- Catch2 or the test framework specified by the document.
- Formatting/static-analysis configuration where practical.
- CI skeleton.
- Basic project README/build instructions if not already present.

## Non-goals

Do not yet implement:

- an order book;
- matching;
- replay;
- strategies;
- Python bindings;
- portfolio accounting;
- exchange adapters.

## Acceptance criteria

- Clean CMake configure.
- Debug build succeeds.
- Release build succeeds.
- Test executable builds and runs.
- Core type tests pass.
- No warnings introduced under the chosen warning configuration.
- CI configuration is syntactically valid.
- Repository is clean after generated files are excluded correctly.

## Finish stage

Run all required build/test commands.

If successful:

1. update `docs/implementation-plan.md`;
2. commit with a focused message such as:

`build: initialise C++20 project and core domain types`

3. push if possible.

Only then continue.

---

# Stage 2 — Limit order book

## Goal

Implement a correct price-time-priority limit order book independently of backtesting/execution simulation.

## Implement

- `Order`
- `PriceLevel`
- `OrderBook`
- bid side;
- ask side;
- order-ID index;
- add;
- cancel;
- modify;
- best bid;
- best ask;
- spread;
- depth;
- mid-price;
- microprice if required by the specification;
- top-N order-book imbalance;
- FIFO ordering at each price;
- book invariants;
- clean handling of empty price levels.

Start from the simple baseline data structures described in the specification unless there is a compelling correctness reason to change them.

Do not prematurely implement a custom high-performance price ladder.

## Required tests

At minimum:

- add bid;
- add ask;
- best bid;
- best ask;
- multiple price levels;
- FIFO ordering;
- cancel first order at a level;
- cancel middle order;
- cancel last order;
- level removal when empty;
- quantity modification;
- price modification;
- invalid/unknown cancellation;
- spread;
- depth;
- mid-price;
- imbalance;
- internal aggregated level quantity equals constituent order quantity;
- no negative quantities;
- valid bid/ask ordering where applicable.

## Acceptance criteria

- Order book behaviour matches the specification.
- All book invariants are automatically testable.
- No full-book scans are required for normal order-ID lookup/cancellation when the intended index exists.
- All tests pass under Debug and Release builds.
- Sanitizer build passes if sanitizers are already configured.

## Finish stage

Commit:

`feat: implement price-time-priority limit order book`

Push if possible.

---

# Stage 3 — Canonical market events + deterministic replay

## Goal

Create a deterministic event-replay engine capable of reconstructing market state from canonical market events.

## Implement

- canonical `MarketEvent`;
- `EventSource` abstraction;
- synthetic/in-memory event source;
- CSV fixture reader;
- chronological ordering using timestamp and sequence;
- snapshot handling;
- incremental updates;
- timestamp validation;
- sequence validation;
- malformed-event handling;
- deterministic replay engine;
- replay statistics where useful.

Keep exchange-specific raw formats outside this layer.

## Required tests

- replay simple snapshot;
- replay add/cancel/modify sequence;
- timestamp ordering;
- sequence ordering;
- invalid sequence regression;
- malformed event;
- snapshot followed by incremental events;
- reset/snapshot semantics;
- identical input produces identical final book;
- identical simulation run produces identical deterministic outputs.

## Acceptance criteria

A small fixture can be loaded and replayed from beginning to end, producing a known correct final order-book state.

Run the same fixture multiple times and verify identical results.

## Finish stage

Commit:

`feat: add deterministic market event replay`

Push if possible.

---

# Stage 4 — Execution simulator

## Goal

Turn replay into a causally correct execution simulator for hypothetical orders.

## Implement

- simulated client orders;
- market orders;
- limit orders;
- crossing limit orders;
- passive limit orders;
- book walking;
- partial fills;
- GTC;
- IOC;
- order cancellation;
- configurable latency;
- maker/taker classification;
- maker/taker fees;
- execution-price calculation;
- L2 queue-position modelling;
- conservative/neutral/optimistic queue models if required by the specification;
- correct event scheduling.

Enforce the causal sequence defined by the specification.

A strategy observing an event at time `t` must not be able to submit an order that affects the market before the simulated exchange-arrival timestamp.

## Required tests

- market order against one price level;
- market order walking several levels;
- partial fill;
- insufficient depth;
- crossing limit;
- passive limit;
- IOC remainder cancellation;
- GTC persistence;
- configured latency respected;
- market changes during latency affect eventual execution;
- maker vs taker classification;
- fee calculation;
- queue-ahead depletion;
- each L2 queue model;
- cancellation handling;
- order state transitions;
- explicit no-look-ahead/causality tests.

## Critical acceptance criterion

Create at least one test constructed specifically so that a broken look-ahead implementation would pass unrealistically, and verify that the correct engine rejects that behaviour.

## Finish stage

Commit:

`feat: implement latency-aware execution simulation`

Push if possible.

---

# Stage 5 — Portfolio accounting + risk controls

## Goal

Maintain correct financial state throughout simulation and prevent invalid strategy exposure.

## Implement

- cash;
- inventory/position;
- average entry where appropriate;
- realised PnL;
- unrealised PnL;
- mark-to-market using the configured mark;
- gross PnL;
- fees/rebates;
- net PnL;
- turnover;
- exposure;
- maximum position;
- maximum order size;
- maximum notional if in specification;
- rejected-order tracking.

## Required tests

- buy then sell;
- profitable round trip;
- losing round trip;
- partial position;
- partial fill accounting;
- multiple fills;
- maker fee;
- taker fee;
- maker rebate;
- realised PnL;
- unrealised PnL;
- net PnL;
- max-position rejection;
- max-order-size rejection;
- rejected orders do not alter financial state.

## Acceptance criteria

For deterministic hand-calculated examples, portfolio values must exactly match expected values within the chosen representation.

## Finish stage

Commit:

`feat: add portfolio accounting and risk controls`

Push if possible.

---

# Stage 6 — Strategy framework

## Goal

Allow trading logic to consume market state and submit orders without being allowed to mutate exchange state directly.

## Implement

- `Strategy` interface;
- restricted `Context`;
- market-event callback;
- fill callback;
- timer callback if specified;
- start/end hooks where appropriate;
- order submission through controlled engine APIs;
- order cancellation through controlled APIs;
- strategy read-only market access.

Implement two reference strategies:

### A. Order-book imbalance strategy

Use configurable:

- book levels;
- entry threshold;
- exit threshold;
- position/order sizing.

### B. Inventory-skewed market maker

Use:

- best bid/ask;
- spread constraints;
- inventory-aware quote skew;
- order cancellation/replacement;
- position limits.

The goal is not to manufacture profitable strategies. They exist to exercise and validate the engine.

## Required tests

- callbacks fire at correct points;
- strategy cannot directly mutate the book;
- orders generated through context;
- position visible correctly;
- fill callbacks correct;
- imbalance calculation drives deterministic expected action;
- market maker changes behaviour according to inventory;
- strategy event ordering respects causality.

## Finish stage

Commit:

`feat: add strategy framework and reference strategies`

Push if possible.

---

# Stage 7 — Python + pybind11 research layer

## Goal

Expose the high-performance C++ engine through a clean Python research API.

## Implement

- pybind11 integration;
- Python package;
- `pyproject.toml`;
- scikit-build-core if specified;
- engine/config bindings;
- result bindings;
- fills;
- orders;
- equity;
- positions;
- metrics;
- convenient conversion to pandas DataFrames;
- plotting helpers;
- experiment configuration;
- parameter grids;
- walk-forward evaluation;
- Python tests.

Prefer a Python API similar in spirit to the specification.

Do not expose unnecessary C++ internals.

If Python strategy callbacks are implemented, clearly document that per-event Python callbacks incur Python/GIL overhead and are not used for core performance benchmarks.

## Required tests

- package builds;
- package installs;
- Python can import module;
- Python launches deterministic C++ backtest;
- results match equivalent C++ run;
- DataFrame conversion correct;
- parameter sweep runs;
- walk-forward split does not leak future data;
- reproducible seed/config behaviour.

## Acceptance criteria

A researcher can install the package and run a complete backtest from Python without writing C++.

## Finish stage

Commit:

`feat: expose simulation engine through Python`

Push if possible.

---

# Stage 8 — Real market data + binary format + performance engineering

## Goal

Run the engine on realistic datasets and generate credible, reproducible performance evidence.

## Implement

### Data

- one real exchange adapter/collector as specified;
- canonical conversion;
- synthetic deterministic dataset generator;
- large synthetic datasets;
- dataset validation.

### Binary format

Implement the compact binary market-event representation described by the specification.

Provide conversion such as:

`microback-convert input.csv output.mkt`

### Benchmarking

Use Google Benchmark for at least:

- add order;
- cancel order;
- modify order;
- best bid/ask;
- trade/application update;
- book walking;
- 1M-event replay;
- 10M-event replay.

Compare:

- CSV replay;
- binary replay.

Record:

- events/sec;
- ns/event;
- benchmark repetitions;
- machine;
- CPU;
- compiler;
- compiler version;
- build type;
- relevant compiler flags;
- commit SHA.

### Profiling

Use Linux `perf` where available.

Measure:

- cycles;
- instructions;
- IPC;
- cache references;
- cache misses;
- branch instructions;
- branch misses.

Create a reproducible profiling script.

## Performance optimisation rule

DO NOT optimise merely because an implementation looks slow.

Use this cycle:

1. benchmark;
2. profile;
3. identify measured bottleneck;
4. formulate optimisation;
5. implement one focused change;
6. rerun benchmark;
7. compare before/after;
8. document result.

Possible optimisations may include those suggested in the specification, but implement only ones justified by measurements.

## Concurrency

If parallel execution is added, first parallelise independent backtests/parameter combinations rather than introducing unnecessary locking into a single instrument's deterministic event loop.

## Acceptance criteria

Produce reproducible benchmark results and at least one measured optimisation with:

- baseline;
- profiler evidence;
- implementation change;
- new measurement;
- percentage improvement.

Do not invent benchmark numbers.

## Finish stage

Commit:

`perf: benchmark and optimise market replay engine`

Push if possible.

---

# Final stage — Research, analysis, documentation and portfolio polish

Only begin this stage once the complete engine is functioning and all earlier acceptance criteria pass.

## Research study 1 — Latency sensitivity

Evaluate the order-book imbalance strategy across latency assumptions such as:

- 0 µs
- 50 µs
- 100 µs
- 250 µs
- 500 µs
- 1 ms

Analyse:

- gross PnL;
- net PnL;
- fill rate;
- slippage;
- turnover;
- adverse selection.

## Research study 2 — Queue-model sensitivity

Compare:

- optimistic;
- neutral;
- conservative L2 queue assumptions.

Demonstrate how execution assumptions change apparent strategy performance.

Explicitly explain why L2 data cannot reveal true queue position.

## Research study 3 — Fees and slippage

Compare:

- frictionless backtest;
- fees only;
- fees + realistic execution;
- latency + fees + realistic execution.

The project should make clear how naive assumptions can create misleading apparent profitability.

## Research study 4 — Passive execution / adverse selection

Using the inventory-skewed market maker, analyse:

- spread capture;
- fill probability;
- post-fill price movement;
- inventory;
- latency sensitivity.

Record future midpoint movement after fills at horizons such as:

- 1 ms;
- 10 ms;
- 100 ms;
- 1 s,

where supported by the data resolution.

## Systems analysis

Document:

- CSV vs binary replay;
- primary hot paths;
- profiling results;
- optimisation performed;
- before/after throughput;
- cache/branch observations where meaningful;
- Python-vs-C++ boundary overhead if measured.

## Required plots

At minimum produce useful plots for:

- equity;
- inventory;
- spread;
- order-book imbalance;
- fills against midpoint;
- gross-vs-fees-vs-slippage-vs-net PnL;
- latency sensitivity;
- queue-model sensitivity;
- adverse selection;
- benchmark throughput.

Do not create visualisations merely for decoration.

---

# Final documentation

Complete the following.

## README.md

It should quickly communicate:

1. what microback is;
2. why it exists;
3. architecture;
4. key features;
5. short example;
6. execution assumptions;
7. benchmark results;
8. research results;
9. build/install instructions;
10. reproducibility;
11. limitations.

The opening description should make clear that this is an event-driven C++20 market-microstructure/backtesting engine, not a pandas OHLC trading bot.

## docs/architecture.md

Explain:

- system boundaries;
- event flow;
- order book;
- replay;
- execution;
- strategies;
- accounting;
- Python boundary;
- data adapters.

## docs/execution-model.md

Explain precisely:

- order arrival;
- latency;
- crossing orders;
- passive orders;
- queue assumptions;
- partial fills;
- fees;
- slippage;
- event ordering;
- no-look-ahead guarantees.

## docs/data-format.md

Explain:

- canonical events;
- tick/lot conversion;
- snapshots;
- incremental updates;
- CSV;
- binary representation;
- validation.

## docs/benchmarking.md

Explain exactly how benchmark numbers are reproduced.

## docs/assumptions.md

Clearly state modelling limitations, especially:

- L2 queue uncertainty;
- simplified market impact;
- latency assumptions;
- venue-specific behaviour not simulated;
- limits of historical backtesting.

---

# Final Definition of Done

Do not declare the project complete until all applicable items below are satisfied:

- C++20 limit-order-book implementation;
- deterministic event replay;
- market and limit orders;
- partial fills;
- GTC/IOC;
- latency simulation;
- maker/taker fees;
- book-walking execution;
- L2 queue model;
- L3 support if required by the specification/data;
- portfolio accounting;
- risk controls;
- imbalance strategy;
- inventory-skewed market maker;
- real market dataset;
- synthetic data generator;
- no-look-ahead tests;
- deterministic/reproducibility tests;
- C++ unit/integration tests;
- sanitizer coverage;
- Google Benchmark suite;
- Linux `perf` profiling where supported;
- Python/pybind11 bindings;
- pandas-compatible research output;
- parameter sweeps;
- walk-forward evaluation;
- research plots;
- CI;
- architecture documentation;
- execution-model documentation;
- assumptions/limitations documentation;
- reproducible build;
- reproducible benchmarks;
- reproducible research experiment;
- clean public-facing README.

---

# Git workflow

At the beginning of each stage:

1. verify `git status`;
2. ensure the previous stage is committed;
3. inspect recent commits;
4. work only on the current stage.

At the end of each stage:

1. run formatting/static checks;
2. build Debug;
3. run tests;
4. run relevant sanitizer checks;
5. build Release where applicable;
6. run stage-specific validation;
7. inspect the final diff;
8. ensure generated/build artefacts are not accidentally tracked;
9. update relevant documentation;
10. commit locally with a focused commit message;
11. push to the configured remote if possible.

Never:

- rewrite history unnecessarily;
- force-push;
- commit secrets;
- commit large raw market datasets;
- commit build directories;
- claim a stage passed if tests/builds failed.

If push fails because no remote/authentication is available, keep the local commit and record the exact reason. Continue only if the local repository is otherwise healthy.

---

# Stage reporting

After completing each stage, produce a short checkpoint report containing:

### Stage completed
Name and objective.

### Implemented
Key files/components.

### Validation
Exact build/test/benchmark commands run and whether they passed.

### Design decisions
Important implementation choices and why they were made.

### Known limitations
Anything intentionally deferred to a later stage.

### Git
Commit SHA and whether it was pushed.

### Next stage
What will be implemented next.

Do not merely say “all tests pass”; actually run them.

---

# Failure handling

If any acceptance criterion fails:

- do not proceed to the next stage;
- diagnose the failure;
- fix it;
- rerun validation.

If a fundamental ambiguity exists between the repository and the specification:

- prefer the specification unless doing so would clearly break an intentional existing design;
- document the conflict and your chosen resolution.

If the required compiler/tool/runtime is unavailable:

- use the closest legitimate substitute where possible;
- document the limitation;
- do not fabricate successful results.

---

# Final review

Once all stages are complete, perform a repository-wide review as if this were being submitted as a portfolio project to a quantitative trading / HFT engineering team.

Look specifically for:

- incorrect market semantics;
- accidental look-ahead;
- unrealistic fill assumptions;
- stale TODOs;
- unnecessary abstractions;
- dead code;
- insufficient tests;
- misleading README claims;
- benchmark claims without reproducibility;
- floating-point misuse;
- ownership/lifetime issues;
- unnecessary allocation in hot paths;
- avoidable copies;
- poor API boundaries;
- Python/C++ boundary issues;
- missing error handling;
- undocumented assumptions.

Fix substantive issues, rerun the full validation suite, and make one final cleanup commit if necessary.

The final repository should be something its author can explain and defend deeply in an interview covering:

- C++20;
- data structures;
- memory/performance trade-offs;
- event-driven systems;
- determinism;
- market microstructure;
- execution modelling;
- backtesting methodology;
- profiling;
- Python/C++ integration.

Begin by reading the complete specification and repository, then create `docs/implementation-plan.md`. Do not start implementing Stage 1 until the plan has been written and checked against the full specification.
