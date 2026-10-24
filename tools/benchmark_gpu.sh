#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
unset DISPLAY
exec luajit tools/run_native.lua "$@"
