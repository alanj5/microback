from pathlib import Path
from microback import Config, Experiment, run
root = Path(__file__).resolve().parents[1]
path = root / 'data/generated/small.csv'
if not path.exists(): path = root / 'tests/fixtures/replay.csv'
result = run(path, config=Config(seed=7, taker_fee_ppm=100))
print(result.summary())
print(Experiment(path).grid({'entry_threshold': [.2, .35], 'latency_us': [0, 1]}))
