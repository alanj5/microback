# Assumptions and limits

- One instrument/venue, deterministic sequential replay. No exchange connectivity
  for trading, multi-asset margin, routing or correlated risk model.
- Prices/lots are positive integers at input. USD conversions require tick and lot
  metadata; a tick*lot is not automatically a dollar. Fee scenario ppm is explicit.
- Cash may go negative; position/order/notional controls model exposure, not a venue's
  credit, borrow, liquidation, auction, stop, hidden/iceberg or self-trade rules.
- Constant nonnegative submit/cancel latency, including the documented equal-time tie
  policy. No network distribution, exchange prioritization or venue ack uncertainty.
- Historical book and simulated consumption are separate. Consumed liquidity is
  capped on updates; replenishment is observable depth growth. There is no endogenous
  impact, feedback, reaction by other agents or re-generated historical path.
- L2 exposes aggregate size, not order identities/age, true queue ahead or the location
  of cancellations. Conservative keeps estimated historical ahead, neutral removes
  a proportional floor share and optimistic removes ahead first. These are scenarios.
- Passive fills use prints at the same side/price as the simulated resting order.
  Price-through inference, inside-spread hypothetical prints and hidden liquidity are
  not synthesized. Quotes away from historical prints can remain unfilled. This is
  a conservative historical-print model, not an exact exchange matching engine.
- Kraken book packets are checksum-validated and converted to final aggregate changes
  (removals before additions); repeated updates within a packet consolidate at a level.
  Canonical strategies may see intermediate level updates at the same receipt time.
  Actual packet atomicity and cross-channel matching-engine order are unavailable.
  Receipt time includes collection network delay; book decreases and trade prints may
  arrive in different channel order, adding execution uncertainty beyond queue models.
- Snapshot resets cancel already resting client orders and preserve pending requests;
  a fresh snapshot gives a new factual book, not continuous historical client priority.
- No forced end liquidation, or future arrival beyond the data horizon. Final positions
  mark to midpoint; active/pending residuals remain visible. Walk-forward files start
  flat independently; training and test windows never carry simulated positions.
- Seed is recorded, but native strategies are deterministic and have no random sampling.
  Trades, signals and costs are not tuned to demonstrate profits. Scenario fees are
  not asserted to match a Kraken account tier.
- Fixed-time return arithmetic uses a positive cash/equity basis, 365.25 days/year and
  as-of marks. Zero variance/no downside/nonpositive equity/too few samples is undefined.
  Annualization of a two-minute capture has no predictive significance. Histories do
  not establish strategy profitability, stationarity or statistical confidence.
- Future labels are calculated after simulation, using as-of mids at supported horizons,
  censored beyond the end and if stale by more than the horizon. Labels never reach
  Strategy/Context. Markout is signed future-mid minus fill, and midpoint move is signed
  future-mid minus arrival-mid; positive means favourable to the fill's side.
- Benchmarks are Release observations on an Apple M4; no cold-disk or cross-machine
  guarantee. macOS sampling is genuine profiler evidence but supplies no Linux perf
  cycles, IPC, cache or branch counters. GCC/Linux CI is configured, unexecuted locally.
- Raw venue/generated large data are excluded from Git. Exact real-data reproduction
  needs the locally preserved capture with its checksum; a recollection differs.
  No redistribution license is asserted for venue data. Optional exact L3, stochastic
  latency, Python callbacks, alternative ladder and parallel batches are not claimed.
