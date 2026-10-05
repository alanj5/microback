#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
python_runtime=${MICROBACK_PYTHON:-python3}
if [[ ! -x .venv/bin/python ]]; then "$python_runtime" -m venv .venv; fi
.venv/bin/python -m pip install .
.venv/bin/python scripts/generate.py data/generated/small.csv
.venv/bin/python scripts/research.py --synthetic-only --output output/demo
printf '%s\n' 'Research tables, manifests and plots: output/demo'
