# Canonical events and CSV replay

MarketEvent stores signed 64-bit nanosecond timestamps, integer ticks and lots,
unsigned 64-bit sequence/order IDs, and one-byte type/side enums. The strongly typed
wrappers preserve these representations. Decimal conversions belong to external
adapters; the canonical CSV reader rejects fractional values. Instruments' tick/lot
scales will be provided by adapter/configuration metadata in the data stage.

## CSV schema

The header is exactly:

```text
timestamp_ns,sequence,order_id,price_ticks,quantity_lots,type,side
```

Type names: Snapshot, Add, Modify, Cancel, Trade. Side names: Buy, Sell. No whitespace,
quoting, comments, extra fields or empty rows. LF and CRLF work. Integers must be in
range, timestamps nonnegative, sequences nonzero. Failures include a row number.
The file reader owns its stream; an istream constructor borrows a stream that must
outlive the reader. Readers cannot be moved/copied because their stream pointer may
refer to their own file member. InMemoryEventSource owns its event vector.

## Snapshot transaction

The specification's single canonical record has no snapshot-boundary flags. This
implementation reserves zero order ID on Snapshot events for boundaries:

- Snapshot, ID 0, price/quantity 0, Buy: begin replacement snapshot.
- Snapshot, nonzero ID and positive price/quantity: one resting order/aggregate row.
- Snapshot, ID 0, price/quantity 0, Sell: commit completed snapshot.

The side on boundary markers is a documented control convention. All rows/markers
of a snapshot must share one timestamp and have distinct increasing sequences.
The replay engine builds the replacement in a separate book; its public book keeps
the last complete state until commit. An invalid/truncated snapshot is never
published. Empty snapshots are permitted. Nesting, unmatched boundaries, snapshot
rows outside a transaction and incremental events inside one are rejected.
`ingest` returns true only when a complete state is observable. Future strategy
callbacks must only run on these published events.

## Incremental semantics

Add requires a new nonzero ID and positive price/quantity. Modify supplies absolute
remaining quantity and price; the existing side must match. Cancel supplies existing
ID and matching side with zero price/quantity. Zero quantity is not a modification.
For L3-style trades, nonzero ID identifies the resting order: price/side must match,
quantity cannot exceed remaining lots, and the fill reduces/removes the order.

For L2 trade prints, Trade ID 0 is informational and does not change reconstructed
depth; subsequent depth updates carry the changed quantities. Trade side identifies
the resting liquidity side, so an aggressive buy is Sell. This convention prevents
subtracting the same trade once as a print and again as a depth update. A later venue
adapter must normalize its aggressor labels accordingly. L2 aggregate rows use stable
synthetic IDs supplied by the adapter; order identity alone does not imply exact L3
queue knowledge. Stage 3 reconstructs history; Stage 4 adds client fills under explicit L2 queue
assumptions described in `execution-model.md`.

## Validation and ordering

Events must already be lexicographically ordered by (timestamp, sequence).
Timestamps cannot decrease; sequences strictly increase globally within a replay,
including equal-time events. Gaps are counted but permitted; they are not silently
filled. Snapshot replacement does not reset sequencing. Explicit `ReplayEngine.reset`
clears book, sequencing and counters and permits a new epoch. External adapters must
handle venue resynchronization explicitly.

Require a completed initial snapshot before incrementals. Unknown IDs, side mismatch,
duplicate IDs, invalid enum/quantity/price, sequence/timestamp regressions and crossed
books fail fast. Snapshot commits run full invariants. Incremental additions/price
changes check the opposite best price before mutation; valid indexed operations
maintain aggregates without a full-book scan. Tests check complete invariants after
mutation sequences. On any replay/parser failure the engine is unusable until explicit
reset, preserving the last complete snapshot or last accepted incremental state.
Statistics report accepted events, committed snapshots and missing sequence values;
replacement resets equal snapshots minus one. There is no lenient/drop mode yet.

Binary replay is a future stage and is not implemented or claimed here.
