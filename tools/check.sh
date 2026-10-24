#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
while IFS= read -r -d '' source; do luac -p "$source"; done < <(find app contracts policies sim validation tools benchmarks -name '*.lua' -print0)
luac -p main.lua
luac -p conf.lua
lua validation/ui_smoke.lua
lua validation/compute_lifecycle.lua
lua validation/json_output.lua
lua validation/apl_compiler.lua
luajit validation/warlock_runner.lua
lua validation/subgroup_canary.lua
luajit validation/specialization.lua
node validation/tooling.mjs
node tools/check_shader.mjs
cc -O2 -Wall -Wextra -Werror -fsyntax-only tools/vulkan_probe.c
