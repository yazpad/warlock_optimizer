#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
(cd third_party/imgui && sha256sum --quiet -c SHA256SUMS)
cmake -S . -B artifacts/imgui-build -G Ninja -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build artifacts/imgui-build --parallel "${FASTSIM_BUILD_JOBS:-4}"
ctest --test-dir artifacts/imgui-build --output-on-failure
