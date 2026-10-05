"""Kraken v2 L2 normalization. Canonical time is local receipt time, never sorted.

A local sequence orders channels because Kraken book has no cross-channel sequence.
Validate every book checksum; reset requires a fresh complete snapshot.
"""
import csv
import json
import zlib
from decimal import Decimal
from pathlib import Path

HEADER = 'timestamp_ns,sequence,order_id,price_ticks,quantity_lots,type,side'.split(',')

def units(value, quantum):
    if isinstance(value, float): raise TypeError('decode decimals without float')
    value, quantum = Decimal(value), Decimal(quantum)
    if not value.is_finite() or not quantum.is_finite() or quantum <= 0 or value < 0:
        raise ValueError('invalid decimal or quantum')
    # Exact integer ratio, independent of Decimal's arithmetic precision context.
    vn, vd = value.as_integer_ratio(); qn, qd = quantum.as_integer_ratio()
    n, remainder = divmod(vn * qd, vd * qn)
    if remainder or n > 2**63 - 1: raise ValueError('unaligned or overflowing decimal')
    return n

def checksum(book):
    text = ''
    for side, reverse in (('Sell', False), ('Buy', True)):
        for price in sorted(book[side], reverse=reverse)[:10]:
            qty = book[side][price]
            for value in (price, qty): text += format(value, 'f').replace('.', '').lstrip('0')
    return zlib.crc32(text.encode())

class Adapter:
    def __init__(self, symbol='BTC/USD', tick='0.1', lot='0.00000001', depth=10):
        if depth not in (10, 25, 100, 500, 1000): raise ValueError('unsupported depth')
        self.symbol, self.tick, self.lot, self.depth = symbol, tick, lot, depth
        self.book = {'Buy': {}, 'Sell': {}}
        self.ids, self.next_id, self.sequence, self.last_time = {}, 1, 0, -1
        self.ready = False
        self.last_trade_id = None
        self.trade_gaps = 0

    def _event(self, t, oid, p, q, typ, side):
        self.sequence += 1
        return [t, self.sequence, oid, p, q, typ, side]

    def process(self, message, received_ns):
        if type(received_ns) is not int or not 0 <= received_ns < 2**63 or received_ns < self.last_time:
            raise ValueError('receipt clock regression or invalid timestamp')
        self.last_time = received_ns
        channel = message.get('channel')
        if channel not in ('book', 'trade'): return []
        if message.get('type') not in ('snapshot', 'update'): raise ValueError('unknown message type')
        events = []
        for data in message['data']:
            if data['symbol'] != self.symbol: raise ValueError('wrong instrument')
            if channel == 'trade':
                if not self.ready or message['type'] == 'snapshot': continue
                tid = data['trade_id']
                if type(tid) is not int or tid <= 0: raise ValueError('invalid trade ID')
                if self.last_trade_id is not None:
                    if tid <= self.last_trade_id: raise ValueError('duplicate/regressed trade ID')
                    self.trade_gaps += tid - self.last_trade_id - 1
                self.last_trade_id = tid
                if data['side'] not in ('buy', 'sell'): raise ValueError('unknown trade side')
                p, q = units(data['price'], self.tick), units(data['qty'], self.lot)
                if not p or not q: raise ValueError('nonpositive trade')
                # Kraken side is taker; canonical trade side identifies resting liquidity.
                events.append(self._event(received_ns, 0, p, q, 'Trade', 'Sell' if data['side']=='buy' else 'Buy'))
                continue
            snapshot = message['type'] == 'snapshot'
            if not snapshot and not self.ready: raise ValueError('update before snapshot')
            old = self.book
            new = {'Buy': {}, 'Sell': {}} if snapshot else {s: dict(v) for s, v in old.items()}
            for field, side in (('bids', 'Buy'), ('asks', 'Sell')):
                for level in data.get(field, []):
                    p, q = Decimal(level['price']), Decimal(level['qty'])
                    if not units(p, self.tick): raise ValueError('zero price')
                    units(q, self.lot)
                    if q == 0: new[side].pop(p, None)
                    else: new[side][p] = q
                prices = sorted(new[side], reverse=side=='Buy')
                new[side] = {p: new[side][p] for p in prices[:self.depth]}
            if new['Buy'] and new['Sell'] and max(new['Buy']) >= min(new['Sell']):
                raise ValueError('crossed venue book')
            if checksum(new) != data['checksum']: raise ValueError('Kraken CRC32 mismatch; resnapshot required')
            if snapshot:
                self.ids = {}
                events.append(self._event(received_ns, 0, 0, 0, 'Snapshot', 'Buy'))
            else:
                # Remove obsolete levels before adding replacements to prevent transient crossing.
                for side in ('Buy', 'Sell'):
                    for p in sorted(old[side].keys() - new[side].keys()):
                        events.append(self._event(received_ns, self.ids.pop((side,p)), 0, 0, 'Cancel', side))
            for side in ('Buy', 'Sell'):
                for p in sorted(new[side], reverse=side=='Buy'):
                    key = side, p
                    if snapshot or key not in self.ids:
                        self.ids[key] = self.next_id; self.next_id += 1
                        typ = 'Snapshot' if snapshot else 'Add'
                    elif old[side].get(p) != new[side][p]: typ = 'Modify'
                    else: continue
                    events.append(self._event(received_ns, self.ids[key], units(p,self.tick), units(new[side][p],self.lot),typ,side))
            self.book, self.ready = new, True
            if snapshot: events.append(self._event(received_ns, 0, 0, 0, 'Snapshot', 'Sell'))
        return events

def convert(raw, output, **settings):
    adapter = Adapter(**settings)
    temp = Path(str(output)+'.tmp')
    try:
        with Path(raw).open() as src, temp.open('w', newline='') as dst:
            writer = csv.writer(dst, lineterminator='\n'); writer.writerow(HEADER)
            for line in src:
                envelope = json.loads(line, parse_float=Decimal)
                writer.writerows(adapter.process(envelope['message'], envelope['received_ns']))
        if not adapter.ready: raise ValueError('capture has no complete snapshot')
        temp.replace(output)
    except Exception:
        temp.unlink(missing_ok=True)
        raise
    return {'events': adapter.sequence, 'trade_sequence_gaps': adapter.trade_gaps,
            'time_basis': 'local receipt nanoseconds', 'checksum': 'all book messages verified'}
