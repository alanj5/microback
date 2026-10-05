# Accounting and submission risk

Money is stored in signed 128-bit atoms, one millionth of one tick*lot. Checked
multiplication/addition fail on overflow. Cash may be negative: this is a margin-style
ledger, with exposure controlled by configured limits, rather than a cash-only wallet.
Fill accounting is transactional. Entry basis is positive for both longs and shorts;
partial closes allocate integer atoms proportionally, rounding down and retaining the
remainder in the open basis. Closing the whole position releases all basis. Reversals
close the old position first and open the residual at the new fill price.

Realised and unrealised PnL exclude fees. Net PnL = gross PnL - accumulated fees;
negative fees are rebates. Equity equals cash plus marked inventory. Exact half-tick
midpoint marks are supported. Average entry is a reporting double derived from stored
integer basis. Slippage is signed execution cost minus decision/arrival midpoint cost;
it is a diagnostic already included in trading PnL, not another amount to subtract.
No risk rejection changes portfolio state.

Submission reserves each live order's entire remaining quantity. Worst-case long and
short outcomes are checked separately: opposite pending orders are not netted because
either side could fill first. Reservation persists until actual fill or exchange cancel,
not the cancellation request. Maximum position and order size are lots. The optional
notional cap is atoms (zero disables it); limits reject and log a terminal Rejected
order. Invalid API parameters still throw rather than create a financial rejection.

The notional cap is a submission check: limits use their cap price, market orders use
the worst currently displayed opposite-side price for their entire requested size,
including any remainder. Missing reference prices cause rejection when this cap is
enabled. This conservative estimate may reject small orders on a deep book. Future
market moves during latency can change actual notional; this is not a guarantee of a
fixed post-arrival currency exposure. Position limits are backed by remaining-lot
reservations and hold across latency. No borrow funding or derivatives margin is modelled.
