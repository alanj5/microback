from pathlib import Path
from types import SimpleNamespace
import math
import pandas as pd
import pytest
from microback import Config, run
from microback.analytics import statistics, adverse_selection, fixed_history

def fixture():
    history=pd.DataFrame({'timestamp_ns':[0,1_000_000,2_000_000,3_000_000],
                          'mid_ticks':[100.,101.,99.,102.], 'equity_ticklots':[100.,101.,99.,102.],
                          'net_pnl_ticklots':[0.,1.,-1.,2.], 'position_lots':[0,1,1,0],
                          'exposure_ticklots':[0.,101.,99.,0.]})
    fills=pd.DataFrame({'client_order_id':[1,2], 'timestamp_ns':[0,2_000_000], 'side':['buy','sell'],
                        'arrival_mid_ticks':[100.,99.], 'price_ticks':[99,100], 'quantity_lots':[2,1], 'liquidity':['maker','taker']})
    orders=pd.DataFrame({'status':['filled','cancelled'],'filled_lots':[2,0]})
    return SimpleNamespace(equity=history,fills=fills,orders=orders)

def test_fixed_time_metrics_and_markout():
    result=fixture(); m=statistics(result,1_000_000)
    assert m['max_drawdown_ticklots']==2 and m['max_drawdown_fraction']==pytest.approx(2/101)
    assert m['maker_fraction']==pytest.approx(2/3)
    assert m['spread_capture_ticks']==1 and m['filled_order_fraction']==.5
    assert math.isfinite(m['annualized_sharpe']) and math.isfinite(m['annualized_sortino'])
    labels=adverse_selection(result,(1,10))
    assert labels.horizon_ms.tolist()==[1,1]
    assert labels.signed_mid_move_ticks.tolist()==[1.,-3.]
    assert labels.markout_ticks.tolist()==[2.,-2.]
    assert result.fills.columns.tolist()==fixture().fills.columns.tolist()
    with pytest.raises(ValueError): fixed_history(result,0)

def test_zero_variance_undefined_and_reporting_no_double_cost():
    result=fixture(); result.equity['equity_ticklots']=100.
    m=statistics(result,1_000_000)
    assert m['annualized_sharpe'] is None and m['annualized_sortino'] is None
    root=Path(__file__).resolve().parents[1]
    r=run(root/'fixtures/replay.csv',config=Config(initial_cash_atoms=1_000_000_000))
    summary=r.summary()
    assert summary['net_pnl_atoms']==summary['gross_pnl_atoms']-summary['fees_atoms']

def test_maxima_include_between_grid_events():
    result=fixture()
    # Risk maxima use observed events, not only the coarser return sampling grid.
    result.equity.loc[1,'position_lots']=50
    result.equity.loc[1,'exposure_ticklots']=5000
    result.equity.loc[1,'equity_ticklots']=10
    metrics=statistics(result,2_000_000)
    assert metrics['max_abs_position_lots']==50
    assert metrics['max_exposure_ticklots']==5000
    assert metrics['max_drawdown_ticklots']==90
    # NumPy abs(int64 minimum) wraps; reporting must cover the native domain.
    result.equity['position_lots']=[0,-2**63,0,0]
    extreme=statistics(result,1_000_000)
    assert extreme['max_abs_position_lots']==2**63
    assert extreme['mean_abs_position_lots']==2**63/4

def test_missing_marks_are_explicitly_undefined(tmp_path):
    import json
    path=tmp_path/'empty-book.csv'
    path.write_text('timestamp_ns,sequence,order_id,price_ticks,quantity_lots,type,side\n'
                    '0,1,0,0,0,Snapshot,Buy\n0,2,0,0,0,Snapshot,Sell\n')
    result=run(path)
    metrics=result.summary()
    assert metrics['net_pnl_atoms'] is None
    assert metrics['max_drawdown_ticklots'] is None
    assert metrics['mean_exposure_ticklots'] is None
    assert metrics['annualized_sharpe'] is None
    json.dumps(metrics,allow_nan=False)
