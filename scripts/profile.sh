#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
out=${1:-output/profile}
mkdir -p "$out"
bench=${MICROBACK_BENCH:-build/bench/microback_bench}
filter=${MICROBACK_PROFILE_FILTER:-execution/1000000/100}
if [[ $(uname -s) == Linux ]]; then
  perf stat -e cycles,instructions,cache-references,cache-misses,branches,branch-misses \
    -o "$out/perf-stat.txt" "$bench" --benchmark_filter="$filter" --benchmark_min_time=10s
  perf record -g -o "$out/perf.data" "$bench" --benchmark_filter="$filter" --benchmark_min_time=10s
  perf report --stdio -i "$out/perf.data" > "$out/perf-report.txt"
elif [[ $(uname -s) == Darwin ]]; then
  "$bench" --benchmark_filter="$filter" --benchmark_min_time=12s > "$out/benchmark.txt" 2>&1 &
  profile_pid=$!
  sample "$profile_pid" 5 1 -file "$out/sample.txt"
  wait "$profile_pid"
  printf '%s\n' 'Linux perf counters unavailable on macOS; sample is statistical stack profiling, not hardware counters.' > "$out/counter-limitations.txt"
else
  printf '%s\n' 'Unsupported profiler platform' >&2; exit 2
fi
