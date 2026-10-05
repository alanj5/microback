# Python research API

`python -m pip install .` builds the C++20 extension using scikit-build-core and
pybind11. `pip install -e .` supports editable Python development (reinstall after
native edits). `python examples/research.py` runs a native strategy and a grid.

```python
from microback import Config, Experiment, run
result = run('events.csv', 'imbalance', {'size': 2},
             Config(latency_ns=50_000, taker_fee_ppm=100, seed=7))
print(result.summary())
print(result.fills)
result.plot().savefig('run.png')
Experiment('events.csv').grid({'entry_threshold': [.2, .35], 'latency_us': [0, 50]})
```

Prices, quantities, IDs and times cross the boundary as integers; authoritative
money is a signed 128-bit native accumulator exposed as exact Python `int` in
`*_atoms` object columns. One million atoms = one tick*lot. `*_ticklots` and mids
are reporting floats. No implicit conversion of floating sizes/latencies is allowed.
Empty tables have stable schemas. Orders retain pending/cancelled/rejected residuals.
The GIL is released for simulation; native strategies cannot observe future history.
No per-event Python callbacks are provided. The seed is recorded; both reference
strategies are deterministic and have no random sampling.

`walk_forward([train.csv, test.csv, ...], grid, train_windows=1)` requires ordered,
nonoverlapping windows, each beginning with a complete snapshot. It selects the
highest average metric on preceding training files, freezes the choice and evaluates
exactly one combination on the next test file. Positions restart flat per file;
window endpoints are validated before any tuning. Tie-breaking follows grid order.
A window is a research split, not a continuous inventory carry between sessions.

`result.summary(interval_ns=10_000_000)` adds event-path maximum drawdown/exposure,
fixed-time mean inventory/exposure and Sharpe/Sortino using 365.25 days/year.
Undefined risk ratios return None. `result.adverse_selection()` creates reporting-only
future midpoint move/markout labels at 1/10/100/1000 ms, with end/staleness censoring.
Money and execution costs remain exact; net = gross − fees. Slippage is a separate
execution-quality measure already embedded in gross PnL, and is not subtracted again.

The specification-style methods `fills_df()`, `orders_df()`, `equity_df()` and
`positions_df()` return copies; `result.metrics` returns the default summary.
The corresponding `.fills`, `.orders`, `.equity` and `.positions` tables remain
available for direct pandas analysis.
