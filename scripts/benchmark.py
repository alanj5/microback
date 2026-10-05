#!/usr/bin/env python3
"""Save actual benchmark output plus environment/commit/workload provenance."""
import argparse
import hashlib
import json
import platform
import os
from pathlib import Path
import subprocess

def command(*args): return subprocess.check_output(args,text=True).strip()
def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''): h.update(b)
    return h.hexdigest()
if __name__=='__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('output'); parser.add_argument('--filter',default='.*'); parser.add_argument('--repetitions',type=int,default=5)
    args=parser.parse_args(); out=Path(args.output); out.mkdir(parents=True,exist_ok=True)
    cmd=['build/bench/microback_bench',f'--benchmark_filter={args.filter}','--benchmark_min_time=0.2s',f'--benchmark_repetitions={args.repetitions}',f'--benchmark_out={out}/results.json','--benchmark_out_format=json']
    cache=Path('build/bench/CMakeCache.txt').read_text()
    compiler=next(line.split('=',1)[1] for line in cache.splitlines() if line.startswith('CMAKE_CXX_COMPILER:'))
    meta={'commit':command('git','rev-parse','HEAD'),'dirty':bool(command('git','status','--porcelain')),
          'platform':platform.platform(),'machine':platform.machine(),'python':platform.python_version(),'command':cmd,
          'compiler':command(compiler,'--version'),
          'cpu':command('sysctl','-n','machdep.cpu.brand_string') if platform.system()=='Darwin' else platform.processor(),
          'ram_bytes':int(command('sysctl','-n','hw.memsize')) if platform.system()=='Darwin' else os.sysconf('SC_PAGE_SIZE')*os.sysconf('SC_PHYS_PAGES'),
          'cache':cache,
          'datasets':{str(p):digest(p) for p in Path('data/generated').glob('100000*.*') if p.suffix in ('.mkt','.csv')},
          'source_sha256':{str(p):digest(p) for d in ('include','src','benchmarks') for p in Path(d).rglob('*') if p.suffix in ('.cpp','.hpp')},
          'benchmark_dependency_commit':command('git','-C','build/bench/_deps/googlebenchmark-src','rev-parse','HEAD')}
    (out/'metadata.json').write_text(json.dumps(meta,indent=2)+'\n')
    with (out/'console.txt').open('w') as f: subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True)

    payload=json.loads((out/'results.json').read_text())
    results=payload['benchmarks']
    if not results or any(row.get('error_occurred') for row in results):
        raise RuntimeError('benchmark workload failed or no measurements')

    for row in results:
        if 'seconds_per_item' in row:
            row['ns_per_event']=row['seconds_per_item'] if row.get('aggregate_name')=='cv' else row['seconds_per_item']*1e9
    (out/'results.json').write_text(json.dumps(payload,indent=2)+'\n')
