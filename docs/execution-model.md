# Execution model (Stage 4)

ExecutionEngine owns deterministic historical ReplayEngine state, client orders,
exchange-arrival actions, fills and a separate consumed-liquidity overlay. It does
not insert client orders into or mutate historical orders. The current mode is an
L2 approximation even when source records identify individual historical orders;
exact L3 client queue modelling is not implemented or claimed.

## Clock and causal ordering

Submit/cancel requests are stamped at the current observable time. Constant latency
is a nonnegative integer nanosecond duration. Arrival is submission time plus latency,
with overflow checked. The internal scheduler orders actions by arrival time then
monotonically increasing action sequence. Requests are queued; `flush()` processes
actions due now after a caller has accepted its current batch of decisions.

When the next external event is at t:

1. Execute internal actions strictly before t against the previously observed book,
   setting each action's clock to its actual arrival timestamp.
2. Apply the external event to historical reconstruction and set current time to t.
3. Apply its trade/queue effects to orders already resting before this event.
4. Execute internal actions arriving exactly at t, in internal sequence order.
5. Return a complete observable state to the caller, who can submit requests and flush.

External events at equal timestamps retain their canonical sequence order. Due actions
run after the first complete external event at that time, before later equal-time
external events; a zero-latency decision runs after the event that generated it.
A trade at the exact timestamp of a new passive order's arrival cannot fill that new
order: trade effects precede insertion. An existing order can fill on a trade tied
with its cancellation arrival. These are explicit tie assumptions, not venue claims.

Snapshot rows are not observable states. No same-time actions or decisions execute
against a partial snapshot; the completed snapshot is published first. Replacement
snapshots cancel active/partially filled resting clients because historical queue
continuity is unknown. Pending requests survive and use the replacement book on arrival.
Submitting during an incomplete snapshot is rejected. This reset policy is a modelling
choice. Final data exhaustion does not execute future pending requests against an
invented continuation of the final book. Invalid input/runtime overflow makes the
engine unusable; construct a new engine for recovery.

## Market, crossing and passive limits

Market buys consume ascending asks; sells consume descending bids. Each consumed level
produces its actual price/quantity fill. Crossing limits consume only prices at or better
than their cap. Insufficient liquidity creates a partial fill; Market remainders and IOC
remainders cancel. GTC limit remainders persist. Filled + remaining + cancelled lots
always equal original lots; terminal orders have zero remaining quantity.

Passive limits require observed trades at their exact price and resting-liquidity side.
Depth moving through a quote alone does not imply a fill. New displayed orders/size
increases are behind an already resting client; they never increase its queue-ahead.
Client FIFO is tracked separately from uncertain historical volume. A later client
cannot reuse a trade volume already allocated to an earlier client. Known client
cancellations remove their remaining size from later clients' ahead volume.

## L2 queue assumptions

At arrival, historical ahead starts at current displayed size less known consumption
and trade prints not yet reflected in depth. Earlier clients at that price contribute
additional known client-ahead lots. Each trade depletes historical ahead, then client
ahead, then fills remaining client lots. Source order IDs do not grant exact client
queue knowledge in this mode.

- Conservative (default): historical cancellations do not reduce ahead.
- Neutral: cancellation removes `floor(ahead * removed / pool_before)` lots ahead.
- Optimistic: historical cancellations remove ahead first.

L2 prints are informational to reconstructed depth, but immediately reduce executable
liquidity and advance passive queues. Subsequent depth decreases first acknowledge
unreflected trades at that price, then treat the residual decrease as cancellation.
Neutral cancellation uses the pool after these already-observed trades. This avoids
advancing twice for one trade. Correlation is aggregate and approximate: venue adapters
must preserve print/depth order; missing/delayed prints can distort inference. Price
modifications remove old-price size and add destination size. All queue policies are
assumptions: L2 data cannot reveal actual order identities or true queue position.

## Liquidity consumption and market impact

A client taker fill consumes available historical liquidity in the overlay, preventing
subsequent clients from reusing the same lots. Available size is the nonnegative
historical size minus consumed lots minus unreflected prints. Historical depth changes
cap consumed lots at current displayed size. Growth increases available lots once;
it does not also release consumed lots. Removal of a level clears its consumption cap;
a replacement snapshot clears the entire overlay.

This is a deliberately simplified counterfactual model. Historical participants do
not react to simulated orders, no hidden liquidity is invented, and historical trades
can concern liquidity a hypothetical client already consumed. This is unsuitable for
large-impact strategies. The baseline allocates/inspects depth and scans client orders;
no performance claim or optimisation is made before benchmarks/profiling.

## Fees and references

Fees are configured as signed integer parts per million: 1 bps = 100 ppm. Positive
rates charge; negative rates rebate. Rates are restricted to +/-1,000,000 ppm.
Maker/taker is classified per fill, so a crossing GTC order may have taker fills first
and maker fills on its resting remainder.

One monetary atom is 1/1,000,000 tick*lot. For integer tick*lot notional N and rate R ppm,
`fee_atoms = N * R`. Thus fractional fees are exact in this representation with no
per-fill rounding. Price/quantity remain signed 64-bit; per-fill notional is checked
against signed 64-bit range and fee products use GCC/Clang signed 128-bit intermediates.
Decimal currency conversion needs instrument tick/lot metadata at reporting boundaries.

Every fill records the decision midpoint and exchange-arrival midpoint (exact half-tick
representation where available), along with execution price. These support signed
latency/arrival slippage decomposition in the accounting stage. Future midpoint/adverse
selection labels must be computed after a run, never shown to strategy code.

Portfolio/risk and strategy callbacks are subsequent stages. No cash/PnL/research
claims are made for the current execution-only API.
