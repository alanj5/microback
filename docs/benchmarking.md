# Benchmark protocol

Release LLVM Clang, C++20, `-O3 -DNDEBUG`, Google Benchmark v1.9.4. Native book core
is the same library used by replay and Python. No Python per-event callbacks.

```
cmake -S . -B build/bench -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DMICROBACK_BENCHMARKS=ON
cmake --build build/bench --parallel 4
python scripts/generate.py data/generated/1000000.csv --size 1M
python scripts/generate.py data/generated/10000000.csv --size 10M
build/bench/microback-convert data/generated/1000000.csv data/generated/1000000.mkt
build/bench/microback-convert data/generated/10000000.csv data/generated/10000000.mkt
python scripts/benchmark.py output/baseline --repetitions 5
scripts/profile.sh output/profile
```

Add/cancel are batches of 1024, excluding cleanup/population. Modify alternates
quantity increases/decreases on one indexed order; best reads both best prices;
trade applies L3 partial decrements; walk scans 100 levels for VWAP; crossing_walk
runs real multi-level simulated market execution, excluding initial reconstruction.
Replay/1M and /10M process pre-generated native vectors (generation outside timing);
execution/1M/10 and /1M/100 process a deterministic L2 stream through execution with
no client strategy/history recording. CSV/binary replay includes open/parse/close
and identical event application from generated files. It measures warm filesystem
cache after calibration, not cold disk throughput. File counts are checked.
Per-event counters divide CPU duration by events; primitive counters count operations,
not market events. Units are identified by workload names. JSON retains individual
runs and aggregate mean/median/stddev/CV; do not treat CI smoke throughput as evidence.

`benchmark.py` saves command, commit/dirty state, compiler/version, CPU/RAM/OS, CMake
cache including flags, source hashes and dataset hashes. A measurement before the
stage commit may have dirty=true; core parent SHA and stored source hashes identify
it. Results are observations on this machine, not performance guarantees. macOS may
schedule across performance/efficiency cores; background load/frequency variation
and uncontrolled filesystem caches limit comparisons. Use repeated runs on the same
workload/build/machine and report spread. No compiler/back-end change between before
and after optimization.

Linux `profile.sh` invokes perf stat for cycles/instructions/cache references/misses/
branches/misses, plus perf record/report call stacks; IPC = instructions/cycles.
macOS invokes `sample` at 1 ms over 5 seconds. Sampling counts are statistical call
stack observations, not elapsed fractions or hardware counters. Linux perf evidence
remains unexecuted here. Clang 23 introduces a __COUNTER__ pedantic diagnostic in
the Google Benchmark dependency; CMake suppresses only that dependency diagnostic
on Clang >=23. Project warnings-as-errors stays enabled.
