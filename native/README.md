# Thin native frontend — first slice

`ui/main.cpp` is an experimental Dear ImGui job client. `frontend/native_job.*` provides a renderer-independent submission/status/cancellation interface. Both GUI and `--headless` call that interface. It launches the existing `tools/run_native.lua` / `tools/vulkan_probe.c` compute path unchanged, with immutable arguments and unique result/work directories.

This is **process isolation, not the planned persistent in-process C engine library**. The existing engine exits on Vulkan failures; retaining process isolation gives the first UI safe failure/cancellation behavior without refactoring its GPU resource ownership prematurely. Repeated jobs still have native process/setup costs. Library extraction, persistent device/pipeline/buffer reuse and wider request support remain TODOs.

## Ownership and performance

- The worker's native C code owns Vulkan, queue submission, shaders/SPIR-V, subgroup enforcement, buffers, batches, cached host-visible readback, report validation and C Welford aggregation. `tools/vulkan_probe.c` is not duplicated or rewritten by this frontend.
- The frontend has no compute buffers, dispatch calls, per-fight report decoder, per-GCD callbacks or per-frame simulation scheduling. It reads bounded batch log messages and validates/loads completed candidate aggregates.
- Dear ImGui uses SDL3/OpenGL3 for window rendering only. Simulation remains Vulkan. A separate renderer avoids coupling UI graphics resources to the compute device; the eventual renderer can be replaced without changing the job interface.
- Compute-priority mode finishes its announcement frame with `glFinish()` before launching the worker, then stops issuing UI rendering/swap calls until the native job ends. It still pumps input, updates the title with coarse progress, and handles Escape/close. This does not assert that the desktop compositor/other applications consume no GPU resources.
- Optional live mode renders at approximately 10 Hz. Its results are labeled `visible-10hz`; paused and headless results have distinct labels. UI frame counts and dependency revision are saved with the worker summary.
- Cancel/close/SIGINT/SIGTERM requests terminate the isolated process group, escalate if needed, drain/reap the worker, remove its owned temporary/partial outputs, and reject accepted throughput. Already completed run artifacts remain historical, but the cancelled job has no successful median summary.

## Build and run (Linux)

```sh
./tools/build_imgui.sh
./tools/run_imgui.sh

# GUI-attached native benchmark, then exit. Default: rendering-paused.
./tools/run_imgui.sh --benchmark

# Same native worker, no SDL/window initialization:
./tools/run_imgui.sh --headless

# Live rendering is a separate comparison mode:
./tools/run_imgui.sh --benchmark --live-ui
```

Dependencies: C++17 compiler, CMake/Ninja, Threads, SDL3 >=3.2, OpenGL, nlohmann-json >=3.10, plus existing native runner tools (LuaJIT, C compiler, Vulkan, glslangValidator, spirv-val, sha256sum). The current machine already supplies these; no network fetch is used by the frontend build. Dear ImGui version/commit, unmodified source hashes and MIT license are pinned under `third_party/imgui/`. Set `FASTSIM_BUILD_JOBS` to change the default four compiler jobs. Build products are ignored under `artifacts/imgui-build/`.

Defaults: three runs of one million fights, shared-feature specialization, required wave32, heap40, 65,536-fight batches and two discarded warmup batches. The fixed suite contains five candidates; total fights must be divisible by five, at least ten. `--generic` selects the existing native generic/default-width control; `--no-subgroup-size` retains specialization without forcing width. `--runs`, `--total-fights`, `--batch-size`, `--warmup-batches`, `--output-dir` and bounded/explicit `--raw-reports` support diagnostics. The current frontend intentionally evaluates the fixed suite; arbitrary candidate editing is deferred.

Each submission gets a fresh directory under `artifacts/imgui/`, or the explicitly supplied **new** `--output-dir`. `native-N.json` preserves each original native result. `summary.json` records timing scope, presentation mode, accepted repeats, medians, UI observations and errors. Engine build key, runner hash, suite/kernel/input identity and variant come from native output. GPU, C aggregation, native full-run, native steady and client-process times remain separate; GUI rates are not inferred from native historical numbers.

## Correctness and performance gates

```sh
# Default: raw million-fight replay, then five alternating aggregate-only pairs.
node tools/benchmark_imgui.mjs

# Separately measure a live-rendering frontend:
node tools/benchmark_imgui.mjs --live-ui
```

The paired harness compares full report-word output across headless/GUI modes before timing acceptance, then checks matching source/input/device/results for all aggregate-only measurements. Raw replay timing is excluded from the performance comparison and its binaries are removed after the gate; replay counts/results are preserved. A passing hardware result requires at least five pairs, median GUI GPU time ≤1.02× headless and steady evaluation ≤1.05× headless. Any correctness failure rejects speed summaries. A measured regression exits nonzero. Paused mode must record zero UI render frames during native jobs.

The initial 10-fight software-Vulkan/offscreen smoke passed exact replay and paused-frame accounting. This is **not** hardware performance certification. The RX 5600 XT **~1.56M GPU fights/sec** baseline and these relative gates must be confirmed in the user's normal hardware/display terminal. Keep the standalone CLI baseline too (`./tools/benchmark_gpu.sh`); passing the same-worker presentation test does not prove the underlying engine still reaches its historical rate.

Small software diagnostics (not speed evidence):

```sh
./tools/run_imgui.sh --headless --allow-software --no-subgroup-size \
  --total-fights 10 --runs 1 --batch-size 3 --warmup-batches 0

# SDL offscreen OpenGL smoke if supported:
SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 ./tools/run_imgui.sh --ui-frames 3
```

Tests: `./tools/build_imgui.sh` builds and runs CTest worker lifecycle/identity/rejection/cancellation tests with synthetic process results. `./tools/check.sh` retains native/shader/APL/reference tests. Neither synthetic rates nor software-Vulkan rates establish the hardware target. Full native library extraction and hardware acceptance gates remain unchecked in TODO.md.
