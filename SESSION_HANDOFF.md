# Fastsim handoff — 2026-10-24

## Next implementation task

**Replace LÖVE with a thin Dear ImGui frontend over the existing native Vulkan/C engine.** The user explicitly chose this direction to preserve GPU performance. The first experimental ImGui frontend now works through an isolated native process client. Persistent in-process engine-library extraction is not implemented yet. See [TODO.md](TODO.md) for the ordered migration and acceptance gates.

Keep compute independent of UI frames and rendering. Refactor `tools/vulkan_probe.c` into a reusable engine/C ABI; have both CLI and frontend call that engine. Retain shaders, specialization, wave32 enforcement, heap40, batch size 65,536, cached readback and C aggregation. Submit immutable jobs to a worker; send only coarse progress and aggregate results to ImGui. No per-fight UI decoding, shader regeneration on cache hits, or per-frame/GCD dispatch pacing.

## Runnable first slice

`./tools/run_imgui.sh` opens the new minimal frontend. `--benchmark` runs the fixed native benchmark with a window attached and exits; `--headless` uses the same worker without SDL initialization. `node tools/benchmark_imgui.mjs` implements raw-word replay first, then five alternating headless/GUI timing pairs. Do not certify hardware performance before that run passes and the standalone CLI still reaches its current baseline.

`native/frontend/native_job.*` owns immutable job snapshots, process-group launch/cancel/reap, coarse progress, native JSON validation and summaries. It runs the existing LuaJIT/C/Vulkan path without changing the compute implementation. The original engine exits on errors, so process isolation safely preserves failure behavior while the future C ABI/resource-lifetime refactor is deferred. Repeated process/setup costs still exist.

Dear ImGui uses SDL3/OpenGL3 for UI rendering only; native compute remains Vulkan. Default compute priority drains the announcement frame before launching a job and issues zero UI render frames during work; Escape/close/signals still cancel. Optional `--live-ui` renders at about 10 Hz and has a distinct label. UI observations, frontend source/build identity, native engine/runner hashes and original native results are saved under `artifacts/imgui/`.

Build, CTest worker lifecycle/rejection/cancellation tests and offscreen UI rendering pass. Small actual software-Vulkan headless/attached jobs matched all raw words; paused mode recorded zero UI frames and live mode rendered during work. A real SIGTERM cancellation test rejected medians and cleaned its owned work directory. These are software/behavioral checks, not hardware speed certification. See [native/README.md](native/README.md).

## Measured position

Frozen workload: five baseline Warlock builds × 200,000 valid 180-second fights, seed 101, fast mode, heap40, shared features + wave32, batches of 65,536, two discarded warmups. RX 5600 XT / RADV NAVI10.

| Path | GPU fights/sec | Steady fights/sec | Full evaluation/run fights/sec |
| --- | ---: | ---: | ---: |
| Historical native controlled experiment | ~1.56M | Not separately recorded | ~1.25M |
| Later user native run | ~1.56M | Not separately recorded | ~1.01M |
| Actual LÖVE wave32 median | **1.331M** | **1.049M** | **0.803M** |

LÖVE cached third run: 757 ms GPU, 190 ms LuaJIT validation/summary, ~15 ms other steady overhead, 176 ms repeated shader preparation/cache lookup, 107 ms warmups; ~1,245 ms total evaluation. The setup figure includes specialization performed before pipeline-cache lookup, not necessarily shader recompilation. Native C aggregation previously took ~12 ms. The GPU gap has not been isolated with back-to-back matching runs; compiler/SPIR-V and clock/load differences are possible causes, not proven explanations.

Evidence: `validation/evidence/latest/love12_wave32_benchmark.json`, historical native `rx5600xt_wave32_abc_cba.json` and `rx5600xt_native_million_probe.json` in the same directory. Timing scopes differ; do not present the table as a paired causal experiment.

## Migration acceptance

- Rerun matched current native and GUI-attached workloads in alternating paired order, at least five pairs. Keep hashes, kernels, seeds, warmups and timing definitions identical.
- Require full million-fight report-word replay, valid completion/counts, explicit subgroup checks and actual-width verification.
- Target no measurable regression. Initial ceilings: GUI median GPU time ≤2% above paired native; matched steady evaluation ≤5% overhead. Investigate systematic loss, preserve C reduction, and do not lower the baseline to the slower LÖVE result.
- Measure headless, visible throttled UI and rendering-paused compute-priority modes separately. Shared-GPU rendering can consume compute capacity; label results honestly and prioritize compute when needed.
- Preserve standalone benchmarking. Only retire LÖVE app/patches/dependencies after equivalent native correctness/performance coverage exists. Stop expanding the LÖVE integration.

## Existing code and commands

Implemented prototype commands (hardware acceptance still open):

```sh
./tools/check.sh
./tools/build_imgui.sh
./tools/run_imgui.sh
./tools/run_imgui.sh --benchmark
./tools/run_imgui.sh --headless
node tools/benchmark_imgui.mjs
./tools/benchmark_gpu.sh
./tools/benchmark_wave32.sh --output-dir artifacts/wave32-next

# Legacy LÖVE comparison path, retained during migration:
./tools/run.sh --benchmark
./tools/run.sh --warlock-smoke --subgroup-size 32
```

`tools/run_native.lua`, `tools/native_support.lua` and `tools/vulkan_probe.c` provide the native path. `benchmarks/love.lua`, `sim/gpu/warlock_runner.lua` and the patched local LÖVE runtime provide the legacy comparison. Existing Lua/Node generators/APL compiler/reference suites remain useful; there is no Python source or dependency.

Basic LÖVE compute/generic/specialized smoke checks and actual wave32 benchmark ran successfully on the user's hardware. The tool sandbox cannot open the normal display or expose `/dev/dri`; SDL offscreen OpenGL UI and software Vulkan diagnostics work. Preserve results instead of trying to benchmark software Vulkan for speed.

## Accuracy and later scope

The actual 5,000-fight LÖVE specialized reference export reproduces **1.0424 DPS MAE**, diagnostic-only with approximate bounds 0–4.1103 and unresolved suite audits. Accepted Forever ≤1 DPS MAE is still open. Exact optimization replay does not close that gate.

A representative 1,000-distinct-APL/talent × 1,000-fight search benchmark is still needed. Mixed inputs are supported; 1.56M/sec is not a measured diverse-search rate. Full UI, search/neural/MCTS features, persistence, classes and packaging follow the engine/frontend migration and accuracy work.
