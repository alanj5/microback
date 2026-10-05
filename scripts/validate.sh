#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
python3 scripts/check_format.py
for mode in debug release sanitize-llvm; do
  cmake --build "build/$mode" --parallel 4
  ctest --test-dir "build/$mode" --output-on-failure
 done
if command -v clang-tidy >/dev/null; then
  while IFS= read -r source; do
    clang-tidy -p build/sanitize-llvm "$source" --quiet --warnings-as-errors='*'
  done < <(rg --files src tests/cpp apps -g '*.cpp')
fi
git diff --check
