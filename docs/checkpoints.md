# Implementation checkpoints

## Stage 1 — Foundation and core types (2026-10-05)

Implemented CMake C++20 configuration, strong 64-bit domains and compact enums,
Catch2 tests, warning policy, ASan/UBSan configuration, style/static analysis,
Ubuntu GCC/Clang CI and README. Empty directories are retained with `.gitkeep`.
The source request was whitespace-normalised without changing its instructions.

Validation (all final commands passed, executed from repository root):

```sh
export PATH=/Users/alanD/Desktop/Projects/.microback-tools/bin:$PATH
cmake -S . -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_SOURCE_DIR_CATCH2="$PWD/build/debug/_deps/catch2-src"
cmake -S . -B build/sanitize-llvm -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ -DMICROBACK_SANITIZERS=ON -DFETCHCONTENT_SOURCE_DIR_CATCH2="$PWD/build/debug/_deps/catch2-src"
cmake --build build/debug --parallel 4
ctest --test-dir build/debug --output-on-failure
cmake --build build/release --parallel 4
ctest --test-dir build/release --output-on-failure
cmake --build build/sanitize-llvm --parallel 4
ctest --test-dir build/sanitize-llvm --output-on-failure
cmake -S . -B build/no-tests -G Ninja -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build/no-tests
python3 scripts/check_format.py
/opt/homebrew/opt/llvm/bin/clang-tidy -p build/sanitize-llvm tests/cpp/test_types.cpp --quiet
/Users/alanD/Desktop/Projects/.microback-tools/bin/python -c 'import yaml; from pathlib import Path; d=yaml.safe_load(Path(".github/workflows/ci.yml").read_text()); assert len(d["jobs"]) == 3'
git diff --check
git check-ignore build/debug/CMakeCache.txt .DS_Store
```

Each configuration passed 2 test cases / 17 assertions. No project compiler or
final clang-tidy diagnostics. Catch2 static registration is excluded from the
throwing-static-initialization check in test configuration only. Catch2 v3.7.1
resolved to `fa43b77429ba76c462b1898d6cd2f2d7a9416b14`.

Initial sanitizer attempt with Apple Clang 17 failed: process sampling showed
a runtime initialization deadlock (`MemoryRangeIsAvailable` -> `get_dyld_hdr` ->
allocator -> `AsanInitFromRtl` -> `StaticSpinMutex`). The stuck process was terminated.
Homebrew LLVM 23.1.2 resolved it, as demonstrated by the successful replacement
build/tests above. `brew install llvm` poured the binaries but its post-install
step reported `unknown install step: configure_clang_system`; the installed
compiler/clang-tidy/runtime were exercised successfully through `/opt/homebrew/opt/llvm`.
See the upstream [Darwin shadow-memory initialization change](https://github.com/llvm/llvm-project/pull/167797).

Design: explicit integer wrappers over specification widths, with validation at
operation boundaries. No book/simulation APIs exist in this stage. GCC and hosted
CI checks were configured but could not be run locally; `/usr/bin/g++` is Apple Clang.

Git: focused foundation commit (identified by this stage's commit in `git log`);
not pushed because `git remote -v` is empty. Repository is healthy locally.
Next: Stage 2 price-time-priority limit order book.
