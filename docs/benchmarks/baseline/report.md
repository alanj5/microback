# Baseline observations

Apple M4, 16 GiB, macOS 26.5, LLVM Clang 23.1.2, Release -O3 -DNDEBUG.
Five repetitions; generation excluded from native replay. Raw JSON, source/dataset
hashes, complete build cache and sampled stacks accompany this table.

| Workload | Median ns/item | Median items/sec | CV of CPU time |
| --- | ---: | ---: | ---: |
| add | 45.78 | 21,843,415 | 0.47% |
| cancel | 44.33 | 22,560,404 | 0.12% |
| modify | 4.32 | 231,712,809 | 0.10% |
| best | 2.40 | 416,767,237 | 0.09% |
| trade | 11.64 | 85,875,740 | 0.88% |
| walk | 288.72 | 3,463,607 | 0.09% |
| crossing_walk | 9991.19 | 100,088 | 0.53% |
| replay/1000000 | 21.54 | 46,433,159 | 0.94% |
| replay/10000000 | 22.23 | 44,989,720 | 10.61% |
| execution/1000000/10 | 463.49 | 2,157,558 | 1.07% |
| execution/1000000/100 | 1164.27 | 858,907 | 0.60% |
| csv/1000000 | 230.73 | 4,334,126 | 1.27% |
| csv/10000000 | 233.70 | 4,278,928 | 0.54% |
| binary/1000000 | 62.45 | 16,013,451 | 0.99% |
| binary/10000000 | 63.71 | 15,695,655 | 3.84% |

For add/cancel, an item is one operation (1024 per iteration); best is a pair of
best-price reads; walk/crossing_walk is a whole multi-level walk. Other primitives
are individual operations; stream workloads count each event. The JSON ns_per_event
numeric counter is nanoseconds despite Google Benchmark rendering its inverse-rate
console suffix as seconds. Use the numeric table or CPU time divided by item count.

Profile: depth materialization and vector allocation/freeing are prominent below
ExecutionEngine::on_market_event. This identifies a candidate quantity lookup change;
no optimization has been applied to this baseline. No Linux hardware counters were
available, and cache/branch/IPC claims are deliberately absent.
