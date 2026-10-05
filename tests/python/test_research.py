import importlib.util
from pathlib import Path
import pandas as pd
from microback import run, walk_forward
ROOT=Path(__file__).resolve().parents[2]

def test_split_snapshots_are_past_only_and_replayable(tmp_path):
    spec=importlib.util.spec_from_file_location('research_script',ROOT/'scripts/research.py')
    research=importlib.util.module_from_spec(spec);spec.loader.exec_module(research)
    source=ROOT/'tests/fixtures/replay.csv'
    windows=research.split_windows(source,tmp_path,count=3)
    ranges=[]
    for path in windows:
        result=run(path,'market_maker')
        assert len(result.equity)>0
        data=pd.read_csv(path)
        assert data.iloc[0]['type']=='Snapshot' and data.iloc[0].order_id==0
        ranges.append((data.timestamp_ns.min(),data.timestamp_ns.max()))
    assert all(a[1]<b[0] for a,b in zip(ranges,ranges[1:]))
    table=walk_forward(windows,{'entry_threshold':[.2,.35]})
    assert (table.train_end_ns<table.test_start_ns).all()
