#!/usr/bin/env python3
"""Capture public Kraken L2/trades. No credentials. One connection, no silent reconnect."""
import argparse
import asyncio
import hashlib
import json
from pathlib import Path
import time
from datetime import datetime, timezone
from microback.adapters.kraken import convert

async def collect(args):
    import websockets
    out = Path(args.output); out.parent.mkdir(parents=True, exist_ok=True)
    started = datetime.now(timezone.utc).isoformat()
    async with websockets.connect('wss://ws.kraken.com/v2', max_size=4_000_000) as ws:
        for channel in ('book', 'trade'):
            params = {'channel': channel, 'symbol': [args.symbol], 'snapshot': channel=='book'}
            if channel == 'book': params['depth'] = args.depth
            await ws.send(json.dumps({'method':'subscribe', 'params':params}))
        end = time.monotonic() + args.seconds
        last = 0
        with out.open('w') as file:
            while time.monotonic() < end:
                try: raw = await asyncio.wait_for(ws.recv(), min(5, end-time.monotonic()))
                except asyncio.TimeoutError: continue
                received = max(last, time.time_ns()); last = received
                message = json.loads(raw)
                if message.get('success') is False: raise RuntimeError(raw)
                # Preserve raw numeric lexemes by inserting the JSON payload verbatim.
                file.write('{"received_ns":'+str(received)+',"message":'+raw+'}\n')
    captured_end=datetime.now(timezone.utc).isoformat()
    canonical = out.with_suffix('.csv')
    result = convert(out, canonical, symbol=args.symbol, tick=args.tick, lot=args.lot, depth=args.depth)
    metadata = {'venue':'Kraken', 'endpoint':'wss://ws.kraken.com/v2', 'symbol':args.symbol,
                'depth':args.depth, 'tick':args.tick, 'lot':args.lot, 'started_utc':started,
                'capture_ended_utc':captured_end, 'conversion_finished_utc':datetime.now(timezone.utc).isoformat(), 'requested_seconds':args.seconds,
                'sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (out,canonical)},
                'license_note':'Public API capture for local research; no redistribution license asserted. Raw files excluded from Git.',
                **result}
    out.with_suffix('.metadata.json').write_text(json.dumps(metadata,indent=2)+'\n')
    print(json.dumps(metadata,indent=2))

if __name__ == '__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('output'); parser.add_argument('--seconds',type=int,default=120)
    parser.add_argument('--symbol',default='BTC/USD'); parser.add_argument('--depth',type=int,default=10)
    parser.add_argument('--tick',default='0.1'); parser.add_argument('--lot',default='0.00000001')
    args=parser.parse_args()
    if args.seconds <= 0: parser.error('positive duration required')
    asyncio.run(collect(args))
