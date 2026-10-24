#!/usr/bin/env bash
set -u
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
printf 'Fastsim native/ImGui diagnostics\n'
for tool in luajit lua node cmake ninja cc glslangValidator spirv-val sha256sum; do
  if command -v "$tool" >/dev/null; then printf '%s: %s\n' "$tool" "$(command -v "$tool")"; else printf '%s: MISSING\n' "$tool"; fi
done
printf '\nFrontend dependencies:\n'
if command -v pkg-config >/dev/null; then
  for package in sdl3 nlohmann_json vulkan gl; do
    printf '%s: ' "$package"
    pkg-config --modversion "$package" 2>/dev/null || printf 'unavailable via pkg-config\n'
  done
fi
printf '\nDisplay: %s | Wayland: %s\n' "${DISPLAY:-unset}" "${WAYLAND_DISPLAY:-unset}"
printf '\nVulkan devices:\n'
if command -v vulkaninfo >/dev/null; then vulkaninfo --summary 2>&1; else printf 'vulkaninfo unavailable\n'; fi
printf '\nBuild: ./tools/build_imgui.sh\n'
printf 'Launch: ./tools/run.sh\n'
printf 'GUI-attached native benchmark: ./tools/run.sh --benchmark\n'
printf 'Same worker, no window: ./tools/run.sh --headless\n'
printf 'Paired correctness/performance gate: node tools/benchmark_imgui.mjs\n'
printf 'Native transport check: node tools/check_headless_transport.mjs\n'
