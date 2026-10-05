# Canonical data and venue normalization

Single venue/instrument, signed 64-bit nonnegative nanoseconds, positive integer
price ticks/lots, unsigned 64-bit nonzero monotonic sequence. The canonical CSV
header is `timestamp_ns,sequence,order_id,price_ticks,quantity_lots,type,side`.
Types: Snapshot/Add/Modify/Cancel/Trade. Sides: Buy/Sell, indicating resting liquidity
for Trade. See replay.md for transactional snapshot markers (ID 0 Buy begin/Sell end),
absolute modifications, nonzero L3 trade IDs and informational ID 0 L2 prints.
Event order is validated rather than silently sorted. Timestamp regression, duplicate/
regressed sequence, missing snapshot, crossing, invalid domain values fail fast;
sequence gaps are counted. Engine input cannot mix symbols.

## Binary v1

All multibyte integers are explicitly little endian. No native struct padding.
16-byte header: magic ASCII `MBACK001` (8), u16 version=1, u16 record size=48,
u32 symbol_id=1. Each 48-byte record:

| Offset | Type | Field |
| --- | --- | --- |
| 0 | u64 | timestamp_ns, limited to INT64_MAX |
| 8 | u64 | sequence |
| 16 | u64 | order_id |
| 24 | i64 | price_ticks |
| 32 | i64 | quantity_lots |
| 40 | u8 | type: Snapshot=0, Add=1, Modify=2, Cancel=3, Trade=4 |
| 41 | u8 | side: Buy=0, Sell=1 |
| 42 | u16 | flags=0 |
| 44 | u32 | symbol_id=1 |

Unknown version, record size, flags, symbol, enum and partial final record fail.
EOF at a record boundary is valid. External SHA256 detects whole-record data loss;
framing itself cannot detect removal of an entire trailing record.
`microback-convert input.csv output.mkt` validates the complete replay and writes via
a temporary file, publishing only after successful finish/close. CLI/Python dispatch
`.mkt` to binary, all other suffixes to CSV. Format contains no tick/lot metadata:
retain the dataset manifest, which defines units and provenance.

## Kraken v2 L2

Adapter: `microback.adapters.kraken`, collector: `scripts/collect_kraken.py`.
Uses public `wss://ws.kraken.com/v2`, BTC/USD depth 10, tick USD 0.1 and lot BTC
0.00000001 in this capture. Decode JSON numbers as Decimal; conversion uses exact
integer ratios and rejects misalignment, negative or overflowing units.
Preserve decimal lexemes for CRC32, apply all sequential updates per message,
truncate both sides to subscribed depth and verify every top-10 checksum. Obsolete
levels generate cancellations before replacement additions; stable synthetic IDs
identify aggregate side/price levels. These IDs do not identify exchange orders.

Canonical time is **local UTC receipt ns**, clamped nondecreasing by collector;
raw exchange timestamps remain in JSONL. Local sequence preserves received channel
and message order; Kraken has no cross-channel book/trade sequence. This models the
observed feed, including network delay, rather than claiming perfect matching-engine
time. Trade IDs are checked for duplicates/regressions and gaps counted. No silent
reconnection: errors fail collection/conversion; a new connection needs a fresh snapshot.
Book checksum cannot certify relative ordering of the separate trade channel.
Trades may be received after the matching book decrease; L2 execution consequently
has feed-order uncertainty in addition to queue uncertainty.

Official references: [book](https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/book),
[trade](https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/trade),
[checksum](https://docs.kraken.com/exchange/guides/websockets/book-checksum-v2).

## Reproduce data

```
python -m pip install '.[data]'
python scripts/collect_kraken.py data/raw/kraken-btc-usd.jsonl --seconds 120
build/release/microback-convert data/raw/kraken-btc-usd.csv data/raw/kraken-btc-usd.mkt
build/release/microback_cli data/raw/kraken-btc-usd.mkt
python scripts/generate.py data/generated/small.csv
python scripts/generate.py data/generated/1M.mkt --size 1M
python scripts/generate.py data/generated/10M.mkt --size 10M
python scripts/generate.py data/generated/100M.mkt --size 100M
```

Raw/generated streams are ignored. The committed manifest records the actual
120-second capture checksum, times, tick/lot units and gap/checksum validation.
A live recollection produces a different stream; preserving the frozen local files
is required to reproduce exact research numbers. Public access is not an asserted
redistribution license: raw data stays local, and the only committed venue sample is
three small book messages used for parser regression. Synthetic generator v1 is
streaming and deterministic across Python versions, with manifest/seed/SHA256;
1M and 10M were generated and validated locally. 100M is available but not timed.
