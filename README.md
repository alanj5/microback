# microback

An event-driven C++20 market microstructure simulator and backtesting engine, with a
planned Python research layer. The project studies execution latency, queue uncertainty
and transaction costs using market events. Profitability is not the success criterion.

Development follows gated vertical stages in [the implementation plan](docs/implementation-plan.md).
Currently Stage 1 provides the build, domain primitives, tests and CI; simulation is not
implemented yet. No throughput or research claims are made.

## Build and test

Requires CMake >=3.24, a C++20 compiler, Git and internet access for the pinned Catch2
3.7.1 test dependency (or CMake FetchContent source override for an offline checkout).

```sh
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug --parallel
ctest --test-dir build/debug --output-on-failure
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --parallel
ctest --test-dir build/release --output-on-failure
cmake -S . -B build/sanitize -DCMAKE_BUILD_TYPE=Debug -DMICROBACK_SANITIZERS=ON
cmake --build build/sanitize --parallel
ctest --test-dir build/sanitize --output-on-failure
python3 scripts/check_format.py
```

Install `clang-format==23.1.2` to run the style check. GCC/Clang Debug/Release and
ASan/UBSan are configured in Ubuntu CI; local validation is recorded in
[checkpoints](docs/checkpoints.md). The current development machine uses Apple Clang.
Integer wrappers retain 64-bit ticks/lots/nanoseconds and IDs without implicit
cross-domain conversion. Input validity belongs to the consuming operation.

On macOS 26.5, Apple Clang 17's ASan runtime deadlocked during initialization on the
development machine. LLVM 23.1.2 passed the same tests. To select it for sanitizers:

```sh
cmake -S . -B build/sanitize-llvm -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DMICROBACK_SANITIZERS=ON
cmake --build build/sanitize-llvm --parallel
ctest --test-dir build/sanitize-llvm --output-on-failure
```
