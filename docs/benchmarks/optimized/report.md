# Quantity lookup optimization

Baseline core parent e0f75fd; baseline suite committed as 1d8bc93. Same LLVM 23.1.2,
Release flags, Apple M4 and deterministic event mix. Five repetitions each; native
generation and history reporting are excluded. Raw source hashes identify the changes.

| Workload | Before median ns/event | After median ns/event | Time reduction | Throughput gain | Before/after CV |
| --- | ---: | ---: | ---: | ---: | ---: |
| execution/1000000/10 | 463.49 | 44.28 | 90.45% | 10.47x | 1.07% / 1.36% |

| execution/1000000/100 | 1164.27 | 60.59 | 94.80% | 19.22x | 0.60% / 1.43% |

execution/1000000/10 individual times before: 464.78, 461.40, 463.49, 467.48, 454.40 ns/event;
after: 45.28, 44.81, 44.28, 43.83, 43.98 ns/event.


execution/1000000/100 individual times before: 1172.49, 1158.08, 1174.27, 1164.27, 1161.35 ns/event;
after: 61.68, 60.59, 60.10, 62.12, 60.40 ns/event.

Hypothesis: visible() copied the entire depth twice for many absolute L2 updates
to retrieve one aggregate. The baseline sample contains OrderBook::depth and vector
allocation/freeing below on_market_event. Add const quantity_at(side,price), returning
the existing map aggregate (zero if absent), then use it in visible(). O(levels) copying
and repeated allocations become O(log levels) lookup. Queue, consumption and scheduling
semantics are unchanged. The optimized sampled execution stack no longer materializes
depth for these lookups. No alternative ladder, concurrency or speculative tuning.

The result applies to an execution-update workload, especially deep books. It is not
a claim that Python research or plain ReplayEngine became 19x faster. Cross-machine
and cache/branch conclusions are unsupported. Repetitions vary with CPU scheduling;
all individual observations are retained rather than declaring a universal speed.

Validation: all 48 CTest cases in Debug/Release/ASan+UBSan, full static/format gate,
7 Python tests, and complete before/after native dictionary comparisons across two
datasets, both strategies, three queue models and two latencies (24 cases). Quantity
lookup tests compare aggregates against depth after add/modify/cancel/clear.
