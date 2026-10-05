#!/usr/bin/env python3
"""Streaming deterministic workload: complete snapshot, L2 trades and absolute updates.

Counts include snapshot framing. Named sizes: small/1M/10M/100M. No PRNG-version dependency.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import struct
HEADER = 'timestamp_ns,sequence,order_id,price_ticks,quantity_lots,type,side'.split(',')
TYPES = ['Snapshot','Add','Modify','Cancel','Trade']

def events(count, seed=7, levels=10):
    if levels < 1 or count < 2*levels+2: raise ValueError('count must fit initial snapshot')
    seq = 1
    yield (0,seq,0,0,0,0,0)
    for side in (0,1):
        for level in range(levels):
            seq += 1
            yield (0,seq,side*levels+level+1,10000-level if side==0 else 10002+level,100+level,0,side)
    seq += 1; yield (0,seq,0,0,0,0,1)
    while seq < count:
        seq += 1; i=seq-2*levels-3; t=(i+1)*100_000
        side=i%2; level=(i//5)%levels; oid=side*levels+level+1
        p=10000-level if side==0 else 10002+level
        # Large periodic imbalances generate round-trip signals; replenishment follows prints.
        q=(400 if (i//100+seed)%2==side else 40)+((i*17+seed)%31)
        if i%5==0: yield (t,seq,0,p,150,4,side)
        else: yield (t,seq,oid,p,q,2,side)

def generate(path, count, seed=7, levels=10):
    path=Path(path); path.parent.mkdir(parents=True,exist_ok=True)
    if path.suffix=='.mkt':
        with path.open('wb') as file:
            file.write(b'MBACK001'+struct.pack('<HHI',1,48,1))
            for t,s,o,p,q,ty,side in events(count,seed,levels):
                file.write(struct.pack('<QQQqqBBHI',t,s,o,p,q,ty,side,0,1))
    else:
        with path.open('w',newline='') as file:
            writer=csv.writer(file,lineterminator='\n'); writer.writerow(HEADER)
            for t,s,o,p,q,ty,side in events(count,seed,levels): writer.writerow((t,s,o,p,q,TYPES[ty],'Buy' if side==0 else 'Sell'))
    digest=hashlib.sha256()
    with path.open('rb') as file:
        for block in iter(lambda:file.read(1024*1024),b''): digest.update(block)
    path.with_suffix(path.suffix+'.json').write_text(json.dumps({'generator':'microback-v1','events':count,'seed':seed,'levels':levels,'sha256':digest.hexdigest()},indent=2)+'\n')

if __name__=='__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('output'); parser.add_argument('--size',choices=['small','1M','10M','100M'],default='small')
    parser.add_argument('--events',type=int); parser.add_argument('--seed',type=int,default=7); parser.add_argument('--levels',type=int,default=10)
    args=parser.parse_args(); generate(args.output,args.events or {'small':20000,'1M':1000000,'10M':10000000,'100M':100000000}[args.size],args.seed,args.levels)
