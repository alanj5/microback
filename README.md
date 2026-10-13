# microback

An event-driven C++20 market microstructure simulator and backtesting engine with
Python research bindings. It reconstructs an integer-tick/lot order book and models
client orders at exchange arrival, so execution assumptions can be studied directly.

The native path includes FIFO book/indexed cancellation, validated deterministic
CSV/binary replay, market/limit GTC/IOC orders, multi-level partial execution,
submit/cancel latency, three estimated L2 queue models, maker/taker fees/rebates,
checked portfolio accounting, exposure reservation and risk rejections. Two native
strategies exercise imbalance signals and inventory-aware maker cancellation/replacement.
Python supplies typed DataFrames, metrics, grids, walk-forward and post-hoc markouts.

## Install and run

Requires Python >=3.10 and a C++20 GCC/Clang compiler; tested locally on macOS arm64
with Apple Clang 17/LLVM 23. CMake/scikit-build-core/pybind11 are build dependencies.

```sh
python -m venv .venv
. .venv/bin/activate
python -m pip install .
python scripts/generate.py data/generated/small.csv
python examples/research.py
```

```python
from microback import Config, run, Experiment
result = run('data/generated/small.csv', 'imbalance', {'size': 1},
             Config(latency_ns=50_000, taker_fee_ppm=200, seed=7))
print(result.summary())
print(result.fills)
result.plot().savefig('run.png')
table = Experiment('data/generated/small.csv').grid(
    {'entry_threshold': [.2, .35, .5], 'latency_us': [0, 50, 250]})
```

**One-command synthetic research demo:** `bash scripts/demo.sh`. It installs a local
wheel and writes frozen configs, tables and all plots under `output/demo`. No credentials.
For the real capture, install `.[data]` and follow [data instructions](docs/data-format.md).
The frozen 120-second BTC/USD capture remains in ignored `data/raw` locally; its
[manifest](docs/datasets/kraken-btc-usd.json) identifies exact checksums. Recollecting
live data gives different results. Neither raw venue nor large synthetic files are in Git.

## Build and test

```sh
cmake -S . -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug --parallel 4
ctest --test-dir build/debug --output-on-failure
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --parallel 4
ctest --test-dir build/release --output-on-failure
python -m pip install '.[test]'
python -m pytest tests/python -q
```

Sanitizers: configure Debug with `-DMICROBACK_SANITIZERS=ON` using Clang/GCC. On
this macOS 26.5 host Apple Clang 17's ASan runtime hung during startup; LLVM 23 works:

```sh
CXX=/opt/homebrew/opt/llvm/bin/clang++ cmake -S . -B build/sanitize-llvm -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMICROBACK_SANITIZERS=ON
cmake --build build/sanitize-llvm --parallel 4
ctest --test-dir build/sanitize-llvm --output-on-failure
```

52 CTest cases, 13 Python tests and package/research gates passed locally. GitHub Actions covers
Ubuntu GCC/Clang Debug/Release, Clang sanitizers, format, Python wheel and benchmark
smoke. Executed validation and CI follow-ups are recorded in
[checkpoints](docs/checkpoints.md).
`microback_cli events.csv` reconstructs the book; `microback-convert input.csv output.mkt`
validates and writes portable 48-byte binary records. Generated streams support
small, 1M, 10M and 100M modes (1M/10M generated/replayed locally).

## Architecture and assumptions

Venue adapter/generator → EventSource → historical replay/book → latency scheduler,
execution overlay and queues → strategy Context/portfolio → native Result → Python.
[Architecture](docs/architecture.md), [execution](docs/execution-model.md),
[accounting/risk](docs/accounting-risk.md), [data format](docs/data-format.md),
[Python API](docs/python-api.md) and [limitations](docs/assumptions.md) define the contracts.

Strategies only observe published current state. Future midpoint labels are computed
post simulation. Integer monetary atoms cross Python without loss; one million atoms
is one tick*lot, not necessarily one dollar. Net PnL is gross minus fees; execution
slippage already affects gross and is not deducted again. Pending/resting residuals
at the end are reported without inventing future fills or forced liquidation.

L2 queue position is unknowable from aggregate depth. Queue/cancellation models are
scenarios, historical impact is simplified, and passive fills require a same-price
historical print. Kraken canonical time preserves received channel order and includes
network delay; there is no cross-channel exchange sequence. No exact L3 client queue,
stochastic latency, Python per-event callbacks or parallel hot-loop claim.

## Measured evidence

On Apple M4 / 16 GiB / macOS 26.5 / LLVM 23.1.2 Release, five repeated observations:
10M binary replay median **63.71 ns/event**, CSV **233.70 ns/event**. A stack-profiled
quantity lookup change reduced the 100-level execution-update workload from
**1164.27 to 60.59 ns/event** (94.80% less time, 19.22x throughput). This excludes
strategy/history/Python reporting and is not an end-to-end speed claim. Raw results,
source/data hashes, repetitions, CV and genuine macOS samples are retained in
[benchmarking](docs/benchmarking.md). Linux hardware counters were unavailable.

[Research report](docs/research/report.md) includes the latency grid, three queue
models, explicit idealized cost ablations, maker markouts and past-only walk-forward.
In the frozen short capture, scenario fees erase the imbalance strategy's small
positive gross PnL; queue assumptions produce different fill counts and losses.
This demonstrates sensitivity to assumptions, not profitability or predictive evidence.
Config/seed/data hashes, tables and plots are committed; exact regeneration uses the
local frozen capture and [reproduction commands](docs/research/report.md).

Development stages and validations: [plan](docs/implementation-plan.md),
[checkpoints](docs/checkpoints.md), [final audit](docs/review.md).
