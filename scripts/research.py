#!/usr/bin/env python3
"""Frozen execution studies. Future markouts are calculated after native simulation."""
import argparse
from dataclasses import asdict, replace
import hashlib
import json
from pathlib import Path
import subprocess
import platform
import importlib.metadata
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from microback import Config, Experiment, run, walk_forward
from microback.analytics import fixed_history

ROOT=Path(__file__).resolve().parents[1]
LATENCIES=[0,50,100,250,500,1000]
QUEUES=['conservative','neutral','optimistic']

def digest(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def save(fig,path): fig.tight_layout(); fig.savefig(path,dpi=150); plt.close(fig)

def mean_move(result, maker_only=False):
    labels=result.adverse_selection((100,))
    if maker_only: labels=labels[labels.liquidity=='maker']
    return float(labels.signed_mid_move_ticks.mean()) if len(labels) else None

def split_windows(source,out,count=4):
    # At a split, carry the reconstructed historical book into a complete fresh
    # snapshot. Future records are never inserted into an earlier window.
    import csv
    from microback.adapters.kraken import HEADER
    with Path(source).open() as f: rows=list(csv.DictReader(f))
    start,end=int(rows[0]['timestamp_ns']),int(rows[-1]['timestamp_ns'])
    boundaries=[start+(end-start+1)*i//count for i in range(1,count)]
    book={}; paths=[]; writer=None; handle=None; seq=0; window=0
    def emit(t,oid,p,q,typ,side):
        nonlocal seq
        seq+=1; writer.writerow([t,seq,oid,p,q,typ,side])
    for row in rows:
        t=int(row['timestamp_ns']); typ=row['type']; oid=int(row['order_id']); side=row['side']
        next_window=sum(t>=boundary for boundary in boundaries)
        if writer is None or next_window != window:
            if handle: handle.close()
            window=next_window
            path=out/f'window-{window}.csv'; paths.append(path); handle=path.open('w',newline=''); writer=csv.writer(handle,lineterminator='\n');writer.writerow(HEADER);seq=0
            if window:
                emit(t,0,0,0,'Snapshot','Buy')
                for id_,(s,p,q) in sorted(book.items()): emit(t,id_,p,q,'Snapshot',s)
                emit(t,0,0,0,'Snapshot','Sell')
        if typ=='Snapshot' and oid==0:
            if side=='Buy': book={}
        elif typ in ('Snapshot','Add','Modify'): book[oid]=(side,int(row['price_ticks']),int(row['quantity_lots']))
        elif typ=='Cancel': book.pop(oid)
        elif typ=='Trade' and oid:
            s,p,q=book[oid]; q-=int(row['quantity_lots'])
            if q: book[oid]=(s,p,q)
            else: book.pop(oid)
        emit(t,oid,int(row['price_ticks']),int(row['quantity_lots']),typ,side)
    if handle: handle.close()
    return paths

def study(source,out,real=False):
    out.mkdir(parents=True,exist_ok=True)
    # Tick*lot units: 1 real tick*lot = $1e-9 for BTC/USD 0.1 tick, 1e-8 BTC lot.
    cfg=Config(seed=7,initial_cash_atoms=10**18 if real else 10**12,
               maker_fee_ppm=100,taker_fee_ppm=200,
               max_order_size=1000000 if real else 10,max_position=1000000 if real else 20)
    params={'size':100000 if real else 1,'entry_threshold':.35,'exit_threshold':.10,'levels':5}
    maker={'size':100000 if real else 1,'position_limit':500000 if real else 10,
           'min_spread':1,'max_spread':10000,'skew_ticks_per_lot':0 if real else 1}
    latency_experiment=Experiment(source,'imbalance',cfg)
    latency=latency_experiment.grid({'latency_us':LATENCIES,**{k:[v] for k,v in params.items()}})
    latency['adverse_mid_move_100ms_ticks']=[mean_move(r) for r in latency_experiment.results]
    latency.to_csv(out/'latency.csv',index=False)
    queue_experiment=Experiment(source,'market_maker',replace(cfg,latency_ns=50000))
    queues=queue_experiment.grid({'queue_model':QUEUES,**{k:[v] for k,v in maker.items()}})
    queues['adverse_mid_move_100ms_ticks']=[mean_move(r,True) for r in queue_experiment.results]
    queues.to_csv(out/'queues.csv',index=False)
    mm_latency_experiment=Experiment(source,'market_maker',cfg)
    mm_latency=mm_latency_experiment.grid({'latency_us':LATENCIES,**{k:[v] for k,v in maker.items()}})
    mm_latency['adverse_mid_move_100ms_ticks']=[mean_move(r,True) for r in mm_latency_experiment.results]
    mm_latency.to_csv(out/'maker-latency.csv',index=False)
    zero=run(source,'imbalance',params,cfg)
    delayed=run(source,'imbalance',params,replace(cfg,latency_ns=250000))
    totals=zero.summary()
    ideal_gross_atoms=totals['gross_pnl_atoms']+totals['decision_slippage_atoms']
    ideal_net_atoms=ideal_gross_atoms-totals['fees_atoms']
    ablations=pd.DataFrame([
        {'scenario':'ideal_mid_same_fills','net_pnl_ticklots':ideal_gross_atoms/1e6,'fills':len(zero.fills)},
        {'scenario':'ideal_mid_same_fills_plus_fees','net_pnl_ticklots':ideal_net_atoms/1e6,'fills':len(zero.fills)},
        {'scenario':'depth_queue_fees_zero_latency','net_pnl_ticklots':zero.summary()['net_pnl_ticklots'],'fills':len(zero.fills)},
        {'scenario':'depth_queue_fees_250us','net_pnl_ticklots':delayed.summary()['net_pnl_ticklots'],'fills':len(delayed.fills)},
    ])
    ablations.to_csv(out/'ablations.csv',index=False)
    maker_result=queue_experiment.results[0]
    labels=maker_result.adverse_selection(); labels=labels[labels.liquidity=='maker']; labels.to_csv(out/'adverse-selection.csv',index=False)
    # Sensitivity also reports all imbalance future midpoint labels post hoc.
    zero.adverse_selection().to_csv(out/'imbalance-adverse-selection.csv',index=False)
    zero.fills.to_csv(out/'fills.csv',index=False); zero.orders.to_csv(out/'orders.csv',index=False)
    maker_result.fills.to_csv(out/'maker-fills.csv',index=False)
    windows=split_windows(source,out)
    wf=walk_forward(windows,{'entry_threshold':[.2,.35,.5],'size':[params['size']], 'exit_threshold':[.1]},config=cfg)
    wf.to_csv(out/'walk-forward.csv',index=False)
    h=fixed_history(zero,1_000_000)
    t=(h.timestamp_ns-h.timestamp_ns.iloc[0])/1e9
    for name,values,ylabel in [('equity',h.net_pnl_ticklots,'Net PnL (tick*lot)'),('inventory',h.position_lots,'Inventory (lots)'),
                                ('spread',h.ask_ticks-h.bid_ticks,'Spread (ticks)'),('imbalance',h.imbalance,'Top-5 imbalance')]:
        fig,ax=plt.subplots(figsize=(9,3));ax.plot(t,values);ax.set(xlabel='Seconds',ylabel=ylabel);save(fig,out/f'{name}.png')
    fig,ax=plt.subplots(figsize=(9,3));ax.plot(t,h.mid_ticks,label='midpoint')
    for side,marker in [('buy','^'),('sell','v')]:
        fills=zero.fills[zero.fills.side==side];ax.scatter((fills.timestamp_ns-h.timestamp_ns.iloc[0])/1e9,fills.price_ticks,marker=marker,s=12,label=side)
    ax.set(xlabel='Seconds',ylabel='Price ticks');ax.legend();save(fig,out/'fills-vs-midpoint.png')
    fig,ax=plt.subplots(figsize=(8,3));s=zero.summary();ax.bar(['Gross','Fees','Decision slippage','Net'],[s['gross_pnl_ticklots'],s['fees_ticklots'],s['decision_slippage_ticklots'],s['net_pnl_ticklots']]);ax.set_ylabel('tick*lot');ax.set_title('Net = gross − fees; slippage is already in gross');save(fig,out/'costs.png')
    fig,axes=plt.subplots(1,2,figsize=(9,3));axes[0].plot(latency.latency_us,latency.net_pnl_ticklots,'o-');axes[0].set(xlabel='Latency (us)',ylabel='Net PnL (tick*lot)');axes[1].plot(latency.latency_us,latency.fill_ratio,'o-');axes[1].set(xlabel='Latency (us)',ylabel='Quantity fill ratio');save(fig,out/'latency.png')
    fig,axes=plt.subplots(1,2,figsize=(9,3));axes[0].bar(queues.queue_model,queues.net_pnl_ticklots);axes[0].set_ylabel('Net PnL (tick*lot)');axes[1].bar(queues.queue_model,queues.fill_ratio);axes[1].set_ylabel('Quantity fill ratio');save(fig,out/'queues.png')
    fig,ax=plt.subplots(figsize=(7,3))
    if len(labels):
        stats=labels.groupby('horizon_ms').agg(move=('signed_mid_move_ticks','mean'),markout=('markout_ticks','mean'),count=('markout_ticks','size'))
        ax.plot(stats.index,stats.move,'o-',label='signed midpoint move');ax.plot(stats.index,stats.markout,'o-',label='markout from fill');ax.set_xscale('log');ax.legend();stats.to_csv(out/'adverse-summary.csv')
    else: ax.text(.5,.5,'No passive fills in this capture/configuration',ha='center',transform=ax.transAxes)
    ax.set(xlabel='Horizon (ms)',ylabel='Ticks');save(fig,out/'adverse-selection.png')
    fig,ax=plt.subplots(figsize=(9,3)); mh=fixed_history(maker_result);ax.plot((mh.timestamp_ns-mh.timestamp_ns.iloc[0])/1e9,mh.position_lots);ax.set(xlabel='Seconds',ylabel='Maker inventory (lots)');save(fig,out/'maker-inventory.png')
    manifest={'data':str(source.relative_to(ROOT) if source.is_relative_to(ROOT) else source),'sha256':digest(source),
              'config':asdict(cfg),'imbalance_parameters':params,'maker_parameters':maker,'latencies_us':LATENCIES,'queue_models':QUEUES,
              'metric_interval_ns':10000000,'future_horizons_ms':[1,10,100,1000],
              'fee_basis':'scenario: maker 100 ppm, taker 200 ppm; not an asserted venue tier',
              'ideal_counterfactual':'same zero-latency depth fills/times/sizes substituted decision-mid prices; no invented extra fills',
              'summary':zero.summary(),'maker_summary':maker_result.summary(),
              'windows':[{'file':p.name,'sha256':digest(p)} for p in windows],
              'versions':{name:importlib.metadata.version(name) for name in ('microback','pandas','numpy','matplotlib')},
              'python':platform.python_version(),'platform':platform.platform(),
              'source_sha256':{str(p.relative_to(ROOT)):digest(p) for p in [ROOT/'CMakeLists.txt',ROOT/'pyproject.toml',*[f for d in ('src','include','python/microback','scripts') for f in (ROOT/d).rglob('*') if f.suffix in ('.py','.hpp','.cpp','.sh')]]},
              'dirty':bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True).strip()),
              'commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()}
    # Tables/plots accompany source/config hash; window data stays excluded from Git.
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2,allow_nan=False)+'\n')

def benchmarks(out):
    baseline=json.loads((ROOT/'docs/benchmarks/baseline/results.json').read_text())['benchmarks']
    optimized=json.loads((ROOT/'docs/benchmarks/optimized/results.json').read_text())['benchmarks']
    names=['csv/10000000','binary/10000000','execution/1000000/100']
    vals=[next(r['events_per_second'] for r in baseline if r['run_name']==n and r.get('aggregate_name')=='median')/1e6 for n in names]
    vals.append(next(r['events_per_second'] for r in optimized if r['run_name']==names[-1] and r.get('aggregate_name')=='median')/1e6)
    fig,ax=plt.subplots(figsize=(8,3));ax.bar(['CSV replay','Binary replay','Execution before','Execution after'],vals);ax.set_ylabel('Million events / CPU second');save(fig,out/'benchmark-throughput.png')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--synthetic',type=Path,default=ROOT/'data/generated/small.csv');parser.add_argument('--real',type=Path,default=ROOT/'data/raw/kraken-btc-usd.csv');parser.add_argument('--output',type=Path,default=ROOT/'output/research');parser.add_argument('--synthetic-only',action='store_true')
    args=parser.parse_args();study(args.synthetic.resolve(),args.output/'synthetic')
    if not args.synthetic_only: study(args.real.resolve(),args.output/'kraken',True)
    benchmarks(args.output)
