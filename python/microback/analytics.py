"""Post-simulation reporting. Future labels never enter native Context."""
import math
import numpy as np
import pandas as pd
from . import SCALE

YEAR_NS = 365.25 * 86400 * 1e9

def fixed_history(result, interval_ns=10_000_000):
    if type(interval_ns) is not int or not 0 < interval_ns <= 2**63-1: raise ValueError('positive integer sampling interval')
    history=result.equity.drop_duplicates('timestamp_ns',keep='last')
    if history.empty: return history
    start,end=int(history.timestamp_ns.iloc[0]),int(history.timestamp_ns.iloc[-1])
    count=(end-start)//interval_ns+1
    if count > 10_000_000: raise ValueError('fixed-time sample exceeds 10M points; increase interval')
    grid=pd.DataFrame({'timestamp_ns':start+np.arange(count,dtype=np.int64)*interval_ns})
    return pd.merge_asof(grid,history,on='timestamp_ns',direction='backward')

def statistics(result, interval_ns=10_000_000):
    history=fixed_history(result,interval_ns)
    if history.empty: return {'annualized_sharpe':None,'annualized_sortino':None}
    observed=result.equity
    eq=history.equity_ticklots.to_numpy(dtype=float)
    if 'equity_atoms' in observed:
        values=[int(x) for x in observed.equity_atoms if x is not None]
        unit=SCALE
    else:
        values=[float(x) for x in observed.equity_ticklots if math.isfinite(x)]
        unit=1
    peak=None; deficits=[]; fractions=[]
    for value in values:
        peak=value if peak is None else max(peak,value)
        deficits.append(peak-value)
        if peak>0: fractions.append((peak-value)/peak)
    max_drawdown=max(deficits)/unit if deficits else None
    def finite(value): return float(value) if math.isfinite(value) else None
    if 'equity_atoms' in history:
        raw=history.equity_atoms.tolist()
        returns=np.array([(int(b)-int(a))/int(a) for a,b in zip(raw,raw[1:])]) if len(raw)>1 and all(x is not None and int(x)>0 for x in raw) else np.array([])
    else:
        returns=np.diff(eq)/eq[:-1] if len(eq)>1 and np.all(eq>0) else np.array([])
    sharpe=sortino=None
    if len(returns)>1 and np.isfinite(returns).all():
        deviation=float(np.std(returns,ddof=1))
        downside=float(np.sqrt(np.mean(np.minimum(returns,0)**2)))
        multiplier=math.sqrt(YEAR_NS/interval_ns)
        if deviation>0: sharpe=float(np.mean(returns)/deviation*multiplier)
        if downside>0: sortino=float(np.mean(returns)/downside*multiplier)
    fills=result.fills
    if len(fills):
        signs=np.where(fills.side=='buy',1,-1)
        weights=fills.quantity_lots.to_numpy(dtype=float)
        maker=fills.liquidity=='maker'
        maker_fraction=float(weights[maker].sum()/weights.sum())
        capture=signs*(fills.arrival_mid_ticks.to_numpy(dtype=float)-fills.price_ticks.to_numpy(dtype=float))
        valid_maker=maker & np.isfinite(capture)
        spread_capture=float(np.average(capture[valid_maker],weights=weights[valid_maker])) if valid_maker.any() else None
    else: maker_fraction=0.0; spread_capture=None
    eligible=result.orders[result.orders.status!='rejected']
    return {'max_drawdown_ticklots':max_drawdown,
            'max_drawdown_fraction':max(fractions) if len(fractions)==len(values) and fractions else None,
            'mean_position_lots':float(history.position_lots.mean()),
            'mean_abs_position_lots':sum(abs(int(x)) for x in history.position_lots)/len(history),
            'max_abs_position_lots':max(abs(int(x)) for x in observed.position_lots),
            'mean_exposure_ticklots':finite(history.exposure_ticklots.mean()),
            'max_exposure_ticklots':finite(observed.exposure_ticklots.max()),
            'maker_fraction':maker_fraction,'taker_fraction':1-maker_fraction if len(fills) else 0.0,
            'spread_capture_ticks':spread_capture,
            'filled_order_fraction':float((eligible.filled_lots>0).mean()) if len(eligible) else 0.0,
            'annualized_sharpe':sharpe,'annualized_sortino':sortino,
            'return_interval_ns':interval_ns,'return_samples':len(returns)}

def adverse_selection(result, horizons_ms=(1,10,100,1000)):
    """As-of midpoint at fill+horizon, censored beyond data end or for stale quotes.

    Signed midpoint move: positive is favourable to the fill's side. Markout also
    includes initial spread capture. Labels are reporting-only floats, never fills.
    """
    history=result.equity.drop_duplicates('timestamp_ns',keep='last').dropna(subset=['mid_ticks'])
    columns=['client_order_id','timestamp_ns','horizon_ms','future_mid_ticks','signed_mid_move_ticks','markout_ticks','quantity_lots','liquidity']
    if history.empty or result.fills.empty: return pd.DataFrame(columns=columns)
    times=history.timestamp_ns.to_numpy(dtype=np.int64); mids=history.mid_ticks.to_numpy(dtype=float)
    rows=[]
    for fill in result.fills.itertuples():
        sign=1 if fill.side=='buy' else -1
        for horizon in horizons_ms:
            if type(horizon) is not int or horizon <= 0: raise ValueError('positive integer horizon')
            target=int(fill.timestamp_ns)+horizon*1_000_000
            if target>int(times[-1]): continue
            index=int(np.searchsorted(times,target,side='right'))-1
            if index<0 or target-int(times[index]) > horizon*1_000_000: continue
            future=float(mids[index])
            rows.append([fill.client_order_id,fill.timestamp_ns,horizon,future,
                         sign*(future-fill.arrival_mid_ticks),sign*(future-fill.price_ticks),
                         fill.quantity_lots,fill.liquidity])
    return pd.DataFrame(rows,columns=columns)
