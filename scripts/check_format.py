#!/usr/bin/env python3
"""Check all project C++ sources without traversing generated build trees."""
import pathlib
import subprocess

root = pathlib.Path(__file__).resolve().parents[1]
files = sorted(p for directory in ("include", "src", "tests/cpp", "apps", "benchmarks")
               for p in (root / directory).rglob("*")
               if p.suffix in {".hpp", ".cpp"})
subprocess.run(["clang-format", "--dry-run", "--Werror", *map(str, files)], check=True)
