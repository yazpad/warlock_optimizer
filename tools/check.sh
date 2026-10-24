#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
while IFS= read -r -d '' source; do luac -p "$source"; done < <(find data policies sim validation tools -name '*.lua' -print0)
lua validation/json_output.lua
lua validation/apl_compiler.lua
luajit validation/specialization.lua
node validation/tooling.mjs
node tools/check_shader.mjs
cc -O2 -Wall -Wextra -Werror -fsyntax-only tools/vulkan_probe.c
cmake -S . -B artifacts/native-check -G Ninja -DCMAKE_BUILD_TYPE=Release -DFASTSIM_BUILD_GUI=OFF
cmake --build artifacts/native-check --target fastsim-job-test --parallel "${FASTSIM_BUILD_JOBS:-4}"
ctest --test-dir artifacts/native-check --output-on-failure
