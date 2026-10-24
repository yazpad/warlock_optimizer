#!/usr/bin/env bash
set -u
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
printf 'Fastsim runtime diagnostics\n'
love_binary=${LOVE_BIN:-love}
if [[ -z "${LOVE_BIN:-}" && -x artifacts/love12/install/bin/love ]]; then love_binary=./artifacts/love12/install/bin/love; fi
printf 'LÖVE (%s): ' "$love_binary"; "$love_binary" --version || true
printf '\nDisplay: %s\n' "${DISPLAY:-unset}"
if command -v xdpyinfo >/dev/null; then
  if xdpyinfo >/dev/null 2>&1; then printf 'Display connection: available\n'; else printf 'Display connection: unavailable\n'; fi
fi
printf '\nVulkan devices:\n'
if command -v vulkaninfo >/dev/null; then vulkaninfo --summary 2>&1; else printf 'vulkaninfo unavailable\n'; fi
printf '\nProduction launch: LOVE_BIN=/path/to/love-12 tools/run.sh\n'
printf 'Compute validation: LOVE_BIN=/path/to/love-12 tools/run.sh --compute-smoke\n'
printf 'Warlock validation: LOVE_BIN=/path/to/love-12 tools/run.sh --warlock-smoke\n'
printf 'Reference suite: LOVE_BIN=/path/to/love-12 tools/run.sh --parity-reference\n'
printf 'Specialized reference: tools/run.sh --parity-reference --specialize-features\n'
printf 'Local LÖVE 12 build: tools/build_love12.sh --download-deps\n'
printf '\nHeadless diagnostic (software Vulkan, does not verify LÖVE):\n'
printf 'env -u DISPLAY VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json node tools/check_headless_transport.mjs\n'
