import json
from decimal import Decimal
from pathlib import Path
import struct
import subprocess
import sys
import pytest
from microback import run
from microback.adapters.kraken import Adapter, checksum, units, convert
ROOT = Path(__file__).resolve().parents[2]

def msg(typ, bids, asks):
    data={'symbol':'BTC/USD', 'bids':[{'price':Decimal(p),'qty':Decimal(q)} for p,q in bids],
          'asks':[{'price':Decimal(p),'qty':Decimal(q)} for p,q in asks]}
    state={'Buy':{Decimal(p):Decimal(q) for p,q in bids},'Sell':{Decimal(p):Decimal(q) for p,q in asks}}
    data['checksum']=checksum(state)
    return {'channel':'book','type':typ,'data':[data]}

def test_adapter_exact_decimal_snapshot_updates_checksum():
    a=Adapter(); snapshot=msg('snapshot',[('100.0','1.00000000')],[('100.2','2.00000000')])
    rows=a.process(snapshot,100)
    assert len(rows)==4 and rows[1][3:5]==[1000,100_000_000]
    update=msg('update',[('100.0','0.50000000')],[('100.2','2.00000000')])
    rows=a.process(update,101); assert rows[0][5]=='Modify' and rows[0][4]==50_000_000
    trade={'channel':'trade','type':'update','data':[{'symbol':'BTC/USD','side':'buy','price':Decimal('100.2'),'qty':Decimal('.00000001'),'trade_id':1}]}
    assert a.process(trade,102)[0][5:]==['Trade','Sell']
    trade['data'][0]['trade_id']=3; a.process(trade,103); assert a.trade_gaps==1
    with pytest.raises(ValueError): a.process(trade,104)
    assert a.process(snapshot,105)[0][5]=='Snapshot'
    update['data'][0]['checksum']=0
    with pytest.raises(ValueError,match='CRC32'): a.process(update,106)
    with pytest.raises(ValueError): Adapter().process(update,100)
    with pytest.raises(ValueError): a.process(snapshot,1)
    with pytest.raises(ValueError): units('100.01','.1')
    with pytest.raises(TypeError): units(.1,'.1')
    with pytest.raises(ValueError): units('1e100','.1')
    assert units('0.00000001','0.00000001')==1

def test_generator_binary_csv_and_large_mode(tmp_path):
    for extension in ('csv','mkt'):
        subprocess.run([sys.executable,str(ROOT/'scripts/generate.py'),str(tmp_path/f'data.{extension}'),'--events','1000'],check=True)
    a,b=run(tmp_path/'data.csv'),run(tmp_path/'data.mkt')
    assert a.summary()==b.summary()
    import pandas as pd
    pd.testing.assert_frame_equal(a.fills,b.fills)
    pd.testing.assert_frame_equal(a.equity,b.equity)
    binary=(tmp_path/'data.mkt').read_bytes()
    assert len(binary)==16+48*1000
    assert struct.unpack('<HHI',binary[8:16])==(1,48,1)
    subprocess.run([sys.executable,str(ROOT/'scripts/generate.py'),str(tmp_path/'repeat.mkt'),'--events','1000'],check=True)
    assert binary==(tmp_path/'repeat.mkt').read_bytes()

def test_captured_venue_fixture(tmp_path):
    output=tmp_path/'captured.csv'
    stats=convert(ROOT/'tests/fixtures/kraken-v2.jsonl',output)
    assert stats['events'] >= 22 and stats['trade_sequence_gaps']==0
    result=run(output,'market_maker',{'size':100000})
    assert len(result.equity) >= 3
    assert (result.equity.ask_ticks > result.equity.bid_ticks).all()

def test_checksum_preserves_latest_price_lexeme_and_rejects_float():
    a=Adapter()
    a.process(msg('snapshot',[('100.0','1.00000000')],[('100.2','2.00000000')]),1)
    update=msg('update',[('100.00','0.50000000')],[('100.2','2.00000000')])
    assert a.process(update,2)[0][5]=='Modify'
    update['data'][0]['bids'][0]['price']=100.0
    with pytest.raises(TypeError): a.process(update,3)
    with pytest.raises(TypeError): units(True,'.1')
    with pytest.raises(TypeError): units('1.0',.1)
