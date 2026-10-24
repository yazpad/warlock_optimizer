#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
unset DISPLAY
exec node tools/benchmark_specialization.mjs --subgroup-size 32 --output-dir artifacts/wave32 "$@"
