# Fastsim handoff — 2026-10-24

## Current position

The native Dear ImGui frontend passed its hardware gate: **1,566,647 GUI-attached GPU fights/sec versus 1,565,063 headless**, five alternating pairs on RX 5600 XT / RADV NAVI10. One million fights / 16 million report words matched exactly across presentation modes. Paused mode issued zero UI render frames during jobs. GPU-time ratio 0.998989 and steady-time ratio 0.995598 pass the ≤2% / ≤5% relative-overhead ceilings.

The fixed workload remains five Warlock builds × 200,000 180-second fights, seed 101, fast mode, shared features + required wave32, heap40, batches of 65,536 and two discarded warmup batches. Native full-run rate was ~1.003M headless / ~0.997M GUI, including preparation/warmups. Do not confuse GPU throughput with full-run throughput.

Evidence: [archived paired experiment](validation/evidence/latest/rx5600xt_imgui_paired/summary.json), including per-run original native results and frontend identities/observations. It certifies the rendering-paused frozen workload on this GPU, not continuous rendering or diverse search populations.

## Backend removal complete

The user authorized removing LÖVE after the hardware pass. Root app/config, LÖVE runners/report decoder/contracts, benchmark/smoke/reference host code, runtime patch/build script, generated project-local build/dependencies and backend-specific tests are removed. Historical evidence remains immutable. The independent upstream checkout outside this project was left untouched.

`tools/run.sh` now launches ImGui through `tools/run_imgui.sh`. Shared JSON encoding moved from the removed app directory to `sim/common/json.lua`. Frozen combat/suite/layout snapshots and exact wrapped Vulkan compiler inputs remain unchanged. Native wrapper compatibility for historical source tags stays; new imports emit native GLSL 450. No LÖVE library/runtime is required.

## Architecture

`native/ui/main.cpp` is the small Dear ImGui/SDL3/OpenGL3 job client. `native/frontend/native_job.*` owns immutable submissions, isolated process-group lifecycle/cancel/reap, coarse progress and completed-result validation. It launches the existing `tools/run_native.lua` / `tools/vulkan_probe.c` path. Vulkan shaders, queues, buffers, readback and C aggregation stay entirely in that engine.

This is a process client. Persistent in-process C ABI/device/pipeline/buffer extraction remains future work; the prototype deliberately retained the original engine's exit-on-error isolation. Do not implement simulation work in UI callbacks or tie batches to UI frames. Keep rendering-paused priority as default; optional live rendering is a separate labeled measurement.

Dear ImGui sources/version/license/hashes are vendored. Linux build requires SDL3, OpenGL, nlohmann-json, CMake/Ninja/C++17 and the native Vulkan/LuaJIT toolchain. Node.js only handles development tooling; there is no Python source/dependency.

## Commands

```sh
./tools/check.sh
./tools/build_imgui.sh
./tools/run.sh
./tools/run.sh --benchmark
./tools/run.sh --headless
node tools/benchmark_imgui.mjs

# Independent live-rendering measurement:
node tools/benchmark_imgui.mjs --live-ui

# Standalone native controls / actual transport check:
./tools/benchmark_gpu.sh
./tools/benchmark_wave32.sh --output-dir artifacts/wave32-next
node tools/check_headless_transport.mjs
```

Each job saves native results and `summary.json` under a new `artifacts/imgui/` directory. The paired tool separates raw replay from aggregate-only timings and removes raw replay binaries afterward. Current guards record source/engine/runner/device identity and reject invalid counts/reports, unsupported required widths, cancelled jobs and evidence overwrites.

The tool sandbox can run offscreen OpenGL UI and software Vulkan diagnostics but cannot access the normal display or `/dev/dri`; hardware comparisons run in the user's terminal. Software rates are not hardware speed evidence.

## Next tasks

1. Extract a persistent native C engine with a small ABI and explicit failure/resource ownership, retaining the same CLI/UI compute implementation and exact replay. Reduce repeated preparation/compilation and device/pipeline/buffer setup without changing the workload.
2. Add arbitrary packed candidate submission and a representative **1,000 distinct APL/talent × 1,000-fight** benchmark. Mixed specialization is supported conceptually, but the current frontend runs the fixed suite and diverse throughput is unmeasured.
3. Extend native detailed/edge/mixed-input, cancellation/retry/recovery and optional live-rendering hardware coverage.
4. Audit/approve Forever mechanics/inputs/APLs/schema and reference precision. Current saved five-case **1.0424 DPS MAE** remains diagnostic-only; exact frontend/optimization replay does not certify ≤1 DPS accuracy.

Search, neural/MCTS policies, persistence, extra classes and broader UI follow those goals. Keep historical evidence immutable and generated outputs under ignored `artifacts/`.
