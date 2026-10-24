#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
(cd third_party/imgui && sha256sum --quiet -c SHA256SUMS)
if [[ ! -x artifacts/imgui-build/fastsim-imgui ]]; then ./tools/build_imgui.sh
else cmake --build artifacts/imgui-build --target fastsim-imgui --parallel "${FASTSIM_BUILD_JOBS:-4}"; fi
exec artifacts/imgui-build/fastsim-imgui "$@"
