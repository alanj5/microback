# Native strategies and result history

Context exposes const book/portfolio/order state and controlled submit/cancel calls.
It does not expose ExecutionEngine, flush, clock advancement, mutable book nodes or
result history. Fill callbacks run after accounting and order status updates at the
actual exchange timestamp. Decisions made inside them still enter the latency queue.

run_backtest calls on_start after the first complete snapshot, then on_market_event
for each published state; it flushes zero-latency requests after the callback. Timer
hooks run at integer intervals from that initial snapshot: strictly earlier timers
precede the next external event; equal-time timers follow the first complete external
state at that timestamp. No timers/actions are invented beyond the data horizon.
on_end runs at the final observable time. Fill and observable-state samples retain
cash, inventory, PnL, exposure, bid/ask/mid and signal; duplicates at equal timestamps
retain insertion order. No future labels enter Context. Constant latency and native
strategies use no randomness; seed is retained in run configuration for reproducibility.

Imbalance enters a configured positive/negative target at the entry threshold and
flattens near the exit threshold. It waits while any client quantity is outstanding.
A requested flatten may fail on exhausted depth; the strategy does not manufacture
liquidity. Default levels/thresholds/size are 5, 0.35, 0.10 and 1 lot.

The maker shifts both reservation quotes against inventory, clamps quotes to remain
passive, respects spread/position constraints, and waits for an old quote's actual
cancellation before replacing it. It avoids accumulating cancel/replace requests while
cancellation is in flight. Fill responses update inventory; quoting responds on market
or timer callbacks. Both strategies validate the engine, without profitability claims.
