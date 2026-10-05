# Baseline order book

The book owns descending bid and ascending ask `std::map` levels. Each level owns a
FIFO `std::list<Order>`. An unordered ID index points at stable level/list nodes.
Add and price-change operations find levels in O(log P). ID lookup is average O(1);
cancellation erases its indexed list node directly, with O(log P) removal if its
price level becomes empty. Quantity-only modifications use list splice for priority.
There is no book scan on the cancellation path.

Prices, lots and nonzero IDs must be positive. Duplicate IDs, invalid sides, unknown
cancel/modify IDs and zero/negative quantities fail with `std::invalid_argument`.
Zero quantity is not shorthand for cancel. Overflow of a level's signed 64-bit
aggregate fails with `std::overflow_error` before changing the order. Modifications
retain the side; side replacement is explicitly cancel plus add.

Quantity decreases and unchanged quantities preserve priority. Quantity increases
move to the back. Any price change moves to the back of the destination level.
Price changes allocate/check the destination before altering the source; indexed
iterators remain stable. The book cannot be copied or moved because its index holds
internal locations. Immutable inspection returns copies, so callers cannot alter levels.

Best prices and two-sided metrics return `std::optional` when unavailable. `mid_price`
returns the floor integer tick. `midpoint` additionally records an exact half-tick bit;
its double conversion is for reporting. Microprice and VWAP floor fractional ticks.
VWAP's side denotes the liquidity side (Sell for a buy walk); it returns no value for
insufficient depth, and rejects a nonpositive requested size. Empty/zero-level imbalance
is zero, a bid-only book is +1 and an ask-only book is -1. Depth is sorted best first.

Weighted integer averages use GCC/Clang `__int128` intermediates to avoid signed
64-bit multiplication overflow, including maximum-size levels/prices. This is a
compiler extension: the supported compilers are GCC and Clang. The floating-point
imbalance is a derived ratio; stored prices and quantities remain authoritative integers.

`valid()` checks positive quantities, unique IDs, side/price consistency, aggregates,
index/node identity, no empty levels and an uncrossed spread. Low-level add does not
match or reject a crossed book: callers can inspect `valid()` after an atomic update.
Replay rejects invalid reconstructed states at its event boundary. Canonical trade
application and atomic snapshots were added in Stage 3; client matching/execution
belong to Stage 4. See `data-format.md` for event semantics.
