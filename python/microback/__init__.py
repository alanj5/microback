"""Python orchestration for deterministic native strategies. Money is in tick*lot atoms."""
from dataclasses import asdict, dataclass, replace
from itertools import product
from pathlib import Path
import csv
import pandas as pd
from . import _core

SCALE = 1_000_000

@dataclass(frozen=True)
class Config:
    latency_ns: int = 0
    queue_model: str = 'conservative'
    maker_fee_ppm: int = 0
    taker_fee_ppm: int = 0
    max_position: int = 2**63 - 1
    max_order_size: int = 2**63 - 1
    max_notional_atoms: int = 0
    initial_cash_atoms: int = 0
    timer_interval_ns: int = 0
    signal_levels: int = 5
    seed: int = 0

    def __post_init__(self):
        for name, value in asdict(self).items():
            if name == 'queue_model':
                if value not in ('conservative', 'neutral', 'optimistic'):
                    raise ValueError('unknown queue model')
                continue
            if type(value) is not int:
                raise TypeError(f'{name} requires an integer')
            lo, hi = (-2**127, 2**127 - 1) if name.endswith('_atoms') else (-2**63, 2**63 - 1)
            if name == 'seed': lo, hi = 0, 2**64 - 1
            if not lo <= value <= hi: raise OverflowError(name)
        if min(self.latency_ns, self.timer_interval_ns, self.max_notional_atoms) < 0:
            raise ValueError('negative interval or notional limit')
        if min(self.max_position, self.max_order_size, self.signal_levels) <= 0:
            raise ValueError('positive risk limits and levels required')
        if max(abs(self.maker_fee_ppm), abs(self.taker_fee_ppm)) > SCALE:
            raise ValueError('fees outside +/- 100%')

_SCHEMAS = {
    'fills': 'client_order_id timestamp_ns side price_ticks quantity_lots liquidity fee_atoms decision_mid_ticks arrival_mid_ticks'.split(),
    'orders': 'client_order_id side type tif limit_price_ticks quantity_lots remaining_lots filled_lots cancelled_lots submitted_ns arrival_ns status'.split(),
    'history': 'timestamp_ns bid_ticks ask_ticks mid_ticks imbalance position_lots cash_atoms realised_pnl_atoms fees_atoms turnover_atoms decision_slippage_atoms arrival_slippage_atoms equity_atoms gross_pnl_atoms net_pnl_atoms unrealised_pnl_atoms exposure_atoms'.split(),
    'rejections': 'client_order_id timestamp_ns reason'.split(),
}

def _frame(rows, name):
    # Construct as object first so nullable huge money integers never round through float.
    frame = pd.DataFrame(rows, columns=_SCHEMAS[name], dtype=object)
    for col in frame:
        if col.endswith('_atoms'):
            frame[col] = frame[col].astype(object)
            frame[col.removesuffix('_atoms') + '_ticklots'] = frame[col].map(
                lambda x: float(x) / SCALE if x is not None else float('nan')).astype(float)
        elif col == 'client_order_id': frame[col] = frame[col].astype('uint64')
        elif col.endswith(('_ns', '_lots')) or col == 'limit_price_ticks' or col == 'price_ticks':
            frame[col] = frame[col].astype('int64')
        elif col in ('bid_ticks', 'ask_ticks'): frame[col] = frame[col].astype('Int64')
        elif col in ('mid_ticks', 'imbalance', 'decision_mid_ticks', 'arrival_mid_ticks'):
            frame[col] = frame[col].astype(float)
    return frame

class Result:
    def __init__(self, raw, config, strategy, parameters, data):
        self.config, self.strategy, self.parameters, self.data = config, strategy, dict(parameters), str(data)
        self.fills = _frame(raw['fills'], 'fills')
        self.orders = _frame(raw['orders'], 'orders')
        self.equity = _frame(raw['history'], 'history')
        self.positions = self.equity[['timestamp_ns', 'position_lots', 'exposure_atoms', 'exposure_ticklots']].copy()
        self.rejections = _frame(raw['rejections'], 'rejections')

    def fills_df(self): return self.fills.copy()
    def orders_df(self): return self.orders.copy()
    def equity_df(self): return self.equity.copy()
    def positions_df(self): return self.positions.copy()

    @property
    def metrics(self): return self.summary()

    def summary(self, *, interval_ns=10_000_000):
        end = self.equity.iloc[-1] if len(self.equity) else {}
        requested = sum(map(int, self.orders.quantity_lots))
        filled = sum(map(int, self.fills.quantity_lots))
        values = {key: end.get(key, None) for key in (
            'gross_pnl_atoms', 'net_pnl_atoms', 'fees_atoms', 'turnover_atoms',
            'decision_slippage_atoms', 'arrival_slippage_atoms')}
        values.update({k.removesuffix('_atoms') + '_ticklots': float(v) / SCALE if v is not None else None
                       for k, v in list(values.items())})
        values.update(fill_ratio=filled / requested if requested else 0.0,
                      fills=len(self.fills), orders=len(self.orders), rejections=len(self.rejections),
                      maker_fraction=float((self.fills.liquidity == 'maker').mean()) if filled else 0.0)
        from .analytics import statistics
        values.update(statistics(self, interval_ns))
        return values

    def adverse_selection(self, horizons_ms=(1, 10, 100, 1000)):
        from .analytics import adverse_selection
        return adverse_selection(self, horizons_ms)

    def plot(self):
        import matplotlib.pyplot as plt
        fig, axes = plt.subplots(3, 1, sharex=True, figsize=(10, 7))
        t = (self.equity.timestamp_ns - self.equity.timestamp_ns.min()) / 1e9
        axes[0].plot(t, self.equity.net_pnl_ticklots); axes[0].set_ylabel('Net PnL (tick*lot)')
        axes[1].plot(t, self.equity.position_lots); axes[1].set_ylabel('Position (lots)')
        axes[2].plot(t, self.equity.imbalance); axes[2].set_ylabel('Imbalance'); axes[2].set_xlabel('Seconds')
        fig.tight_layout()
        return fig

def run(data, strategy='imbalance', parameters=None, config=None, *, latency_us=None):
    config = config or Config()
    if not isinstance(config, Config): raise TypeError('config requires Config')
    if latency_us is not None:
        if type(latency_us) is not int: raise TypeError('latency_us requires integer microseconds')
        config = replace(config, latency_ns=latency_us * 1000)
    parameters = dict(parameters or {})
    allowed = {'imbalance': {'levels', 'entry_threshold', 'exit_threshold', 'size'},
               'market_maker': {'size', 'position_limit', 'min_spread', 'max_spread', 'skew_ticks_per_lot'}}
    if strategy not in allowed: raise ValueError('unknown strategy')
    if parameters.keys() - allowed[strategy]: raise ValueError('unknown strategy parameter')
    for k, v in parameters.items():
        if k not in ('entry_threshold', 'exit_threshold') and type(v) is not int:
            raise TypeError(f'{k} requires integer')
    return Result(_core.run_native(path=str(data), strategy=strategy, parameters=parameters, config=asdict(config)),
                  config, strategy, parameters, data)

class Experiment:
    def __init__(self, data, strategy='imbalance', config=None):
        self.data, self.strategy, self.config = data, strategy, config or Config()
        self.results = []

    def grid(self, grid):
        keys = list(grid)
        if any(not grid[k] for k in keys): raise ValueError('empty parameter grid')
        rows, self.results = [], []
        for values in product(*(grid[k] for k in keys)):
            choice = dict(zip(keys, values)); cfg, params = {}, {}
            for k, v in choice.items():
                if k == 'latency_us':
                    if type(v) is not int: raise TypeError('latency_us requires integer microseconds')
                    cfg['latency_ns'] = v * 1000
                elif k in asdict(self.config): cfg[k] = v
                else: params[k] = v
            result = run(self.data, self.strategy, params, replace(self.config, **cfg))
            self.results.append(result); rows.append({**choice, **result.summary()})
        return pd.DataFrame(rows)

def _range(path):
    with Path(path).open(newline='') as file:
        times = [int(row['timestamp_ns']) for row in csv.DictReader(file)]
    if not times: raise ValueError('empty window')
    return min(times), max(times)

def walk_forward(windows, grid, *, strategy='imbalance', config=None,
                 train_windows=1, metric='net_pnl_ticklots'):
    """Each file is an independent complete snapshot+stream, ordered and disjoint.

    Choose parameters on previous train_windows only, then freeze for the next file.
    Positions start flat for each window. Ties retain grid insertion order.
    """
    if type(train_windows) is not int or train_windows < 1: raise ValueError('positive training window count')
    ranges = [_range(p) for p in windows]
    if any(a[1] >= b[0] for a, b in zip(ranges, ranges[1:])):
        raise ValueError('windows must be chronological and nonoverlapping')
    rows = []
    keys = list(grid)
    if any(not grid[k] for k in keys): raise ValueError('empty parameter grid')
    for i in range(train_windows, len(windows)):
        scores = []
        for path in windows[i-train_windows:i]:
            table = Experiment(path, strategy, config).grid(grid)
            scores.append(table[metric].astype(float).to_numpy())
        import numpy as np
        average = np.mean(scores, axis=0)
        if not np.isfinite(average).all(): raise ValueError('undefined training metric')
        winner = int(np.argmax(average))
        choices = list(product(*(grid[k] for k in keys)))
        frozen = dict(zip(keys, choices[winner]))
        experiment = Experiment(windows[i], strategy, config)
        test = experiment.grid({k: [v] for k, v in frozen.items()}).iloc[0].to_dict()
        rows.append({'train_start_ns': ranges[i-train_windows][0], 'train_end_ns': ranges[i-1][1],
                     'test_start_ns': ranges[i][0], 'test_end_ns': ranges[i][1],
                     'parameters': frozen, 'training_score': float(average[winner]), **test})
    return pd.DataFrame(rows)
