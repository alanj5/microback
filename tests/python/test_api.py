from dataclasses import replace
from pathlib import Path
import pandas as pd
import pytest
from microback import Config, Experiment, run, walk_forward
FIXTURE = Path(__file__).resolve().parents[1] / 'fixtures/replay.csv'

def test_native_equivalence_and_reproducibility(tmp_path):
    # Same hand-calculated stream/assertions as C++ test_strategy.cpp.
    path = tmp_path / 'case.csv'
    path.write_text('timestamp_ns,sequence,order_id,price_ticks,quantity_lots,type,side\n'
                    '0,1,0,0,0,Snapshot,Buy\n0,2,1,100,30,Snapshot,Buy\n'
                    '0,3,2,102,10,Snapshot,Sell\n0,4,0,0,0,Snapshot,Sell\n'
                    '10,5,0,100,31,Trade,Buy\n20,6,1,100,10,Modify,Buy\n')
    a, b = run(path, config=Config(seed=7)), run(path, config=Config(seed=7))
    pd.testing.assert_frame_equal(a.equity, b.equity)
    pd.testing.assert_frame_equal(a.fills, b.fills)
    assert len(a.orders) == 2 and len(a.fills) == 1
    assert a.fills.iloc[0].price_ticks == 102 and a.fills.iloc[0].quantity_lots == 1
    assert a.orders.iloc[-1].status == 'cancelled'
    assert a.equity.iloc[-1].cash_atoms == -102_000_000
    assert a.equity.iloc[-1].net_pnl_atoms == -1_000_000
    assert a.fills.timestamp_ns.dtype == 'int64'
    assert a.equity.cash_atoms.dtype == object
    assert a.positions.position_lots.dtype == 'int64'

def test_exact_large_money_and_empty_frames():
    value = 2**100 + 123
    result = run(FIXTURE, parameters={'entry_threshold': 1.0}, config=Config(initial_cash_atoms=value))
    assert result.equity.iloc[-1].cash_atoms == value
    assert len(result.fills) == 0 and 'fee_atoms' in result.fills
    assert result.fills.price_ticks.dtype == 'int64'
    with pytest.raises(TypeError): Config(latency_ns=0.1)
    with pytest.raises(ValueError): run(FIXTURE, parameters={'typo': 1})
    with pytest.raises(TypeError): run(FIXTURE, latency_us=.5)

def test_grid_and_past_only_walkforward(tmp_path):
    grid = {'entry_threshold': [.2, .35], 'latency_us': [0, 1]}
    assert len(Experiment(FIXTURE).grid(grid)) == 4
    windows = []
    for offset in (0, 1000, 2000):
        lines = FIXTURE.read_text().splitlines()
        rows = [','.join([str(int(row.split(',')[0])+offset), *row.split(',')[1:]]) for row in lines[1:]]
        path = tmp_path / f'{offset}.csv'; path.write_text('\n'.join([lines[0], *rows])+'\n'); windows.append(path)
    result = walk_forward(windows, grid)
    assert len(result) == 2
    assert (result.train_end_ns < result.test_start_ns).all()
    # Changing only the unseen test stream cannot change its selected parameters.
    altered = windows[1].read_text().replace(',100,', ',90,')
    windows[1].write_text(altered)
    changed = walk_forward(windows[:2], grid)
    assert changed.iloc[0].parameters == result.iloc[0].parameters
    assert changed.iloc[0].training_score == result.iloc[0].training_score
    with pytest.raises(ValueError): walk_forward(windows[::-1], grid)

def test_plot():
    import matplotlib
    matplotlib.use('Agg')
    fig = run(FIXTURE).plot()
    assert len(fig.axes) == 3
