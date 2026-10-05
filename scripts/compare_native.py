#!/usr/bin/env python3
"""Compare complete native results to a preserved pre-change extension, not summaries."""
import importlib.util
from pathlib import Path
import sys
from dataclasses import asdict
from microback import Config, _core
spec=importlib.util.spec_from_file_location('baseline._core',sys.argv[1])
baseline=importlib.util.module_from_spec(spec); spec.loader.exec_module(baseline)
cases=0
for data in sys.argv[2:]:
    for strategy, parameters in (('imbalance',{'size':10000,'entry_threshold':.2}),
                                 ('market_maker',{'size':10000,'position_limit':100000,'skew_ticks_per_lot':0})):
        for queue in ('conservative','neutral','optimistic'):
            for latency in (0,50000):
                args={'path':str(data),'strategy':strategy,'parameters':parameters,
                      'config':asdict(Config(queue_model=queue,latency_ns=latency,taker_fee_ppm=100,maker_fee_ppm=-10))}
                assert baseline.run_native(**args)==_core.run_native(**args), (data,strategy,queue,latency)
                cases+=1
print(f'{cases} complete result comparisons passed')
