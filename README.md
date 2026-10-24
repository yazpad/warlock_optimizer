# Fastsim

Native Vulkan combat simulation with a thin Dear ImGui frontend, starting with Warlock. **The ImGui frontend preserves the ~1.56M GPU fights/sec baseline** on RX 5600 XT in compute-priority mode. LÖVE application code, runtime patches, build scripts and generated build dependencies have been removed.

The GUI submits immutable jobs to the existing LuaJIT/C/Vulkan worker. The engine owns shaders, wave32, dispatch, readback, completion validation and C aggregation. ImGui receives coarse progress and completed candidate summaries. Rendering pauses during jobs by default; input/cancellation remain active. A persistent in-process C engine library is future work.

## Build and run

```sh
./tools/build_imgui.sh
./tools/run.sh

# Run the fixed native benchmark with ImGui attached, then exit:
./tools/run.sh --benchmark

# Same worker, no window initialization:
./tools/run.sh --headless

# Optional live rendering at about 10 Hz; measure this separately:
./tools/run.sh --benchmark --live-ui
```

`tools/run_imgui.sh` is also available directly. Build products and job outputs live under ignored `artifacts/`. Each job gets a fresh output directory containing `native-N.json` and `summary.json`; use `--output-dir PATH` for an explicit new directory. Escape cancels an active job. The first frontend currently runs the fixed suite; arbitrary APL/talent/configuration editing is deferred.

Linux dependencies: C++17 compiler, CMake/Ninja, Threads, SDL3 >=3.2, OpenGL, nlohmann-json >=3.10; native compute also needs Lua/LuaJIT, a C compiler, Vulkan headers/loader/driver, `glslangValidator`, `spirv-val` and `sha256sum`. Dear ImGui sources, commit, hashes and MIT license are vendored under `third_party/imgui/`; no network fetch is required by the frontend build. SDL3/OpenGL3 renders only the UI; simulation stays Vulkan. Set `FASTSIM_BUILD_JOBS` to adjust the default four compiler jobs.

There is no Python source or required command. Node.js 22+ is development tooling for imports, comparisons and benchmark orchestration; the GUI and native job execution do not require Node.js.

## Measured hardware result

Five fixed Warlock builds × 200,000 valid fights, 180-second duration, seed 101, fast reports, shared features, required wave32, heap40, batches of 65,536, two discarded warmups. RX 5600 XT / RADV NAVI10. Five alternating headless/GUI timing pairs passed:

| Median | Native/headless | ImGui attached, rendering paused |
| --- | ---: | ---: |
| GPU fights/sec | 1,565,063 | **1,566,647** |
| Steady evaluation time | 671.069 ms | **668.115 ms** |
| Native full-run fights/sec | 1,002,581 | 996,948 |
| Client-process wall time | 1,006.967 ms | 1,011.407 ms |

Every raw report word matched across **one million fights / 16 million words** before timing. The UI issued **zero rendering frames during native work**. GPU time ratio was 0.998989 and steady time ratio 0.995598, within the ≤2% GPU-time / ≤5% steady-time overhead gates. Treat the small differences as run-to-run variation, not a speedup claim. Evidence: [archived paired experiment](validation/evidence/latest/rx5600xt_imgui_paired/summary.json).

This establishes the paused-rendering, fixed-workload result on the tested GPU. Continuous live rendering, other devices, diverse policies and cold startup need separate measurements. Historical native full-run timing reached ~1.25M/sec with an earlier orchestrator; it is not the current GUI full-run rate.

## Benchmark commands and timing

```sh
# Raw report replay, then five alternating aggregate-only timing pairs:
node tools/benchmark_imgui.mjs

# Independent live-rendering comparison:
node tools/benchmark_imgui.mjs --live-ui

# Standalone native baseline and generic/specialized controls:
./tools/benchmark_gpu.sh
./tools/benchmark_wave32.sh --output-dir artifacts/wave32-next
./tools/benchmark_specialization.sh --output-dir artifacts/specialization-next
```

Default GUI/headless jobs use three million-fight runs. `--runs`, `--total-fights`, `--batch-size`, `--warmup-batches`, `--generic`, `--no-subgroup-size` and explicit `--raw-reports` support diagnostics. Total fights must be divisible by the five candidates, at least ten. Altered workloads are not directly comparable to the million-fight baseline. Generic currently uses the driver-default subgroup width; generic wave32 is a future native control. Unsupported required wave32 rejects without fallback.

GPU rates use the engine's Vulkan timestamps. Steady evaluation includes measured dispatch/readback plus C aggregation. Native wall time includes job preparation, pipeline setup, warmups and execution; client wall additionally includes worker-process startup/result loading. Window/application startup is outside the per-job timer. Keep these scopes separate.

The paired harness performs raw replay separately from aggregate-only timings, retains gate results and removes replay binaries afterward. It checks source/input/device/results and zero paused-mode UI frames. At least five hardware pairs are required for `passed-relative-overhead`; software or shorter experiments remain diagnostic-only. A correctness failure rejects speed summaries, and a measured regression exits nonzero. The historical absolute GPU baseline and relative overhead are separate checks.

## Validation and reference accuracy

```sh
./tools/check.sh
./tools/doctor.sh
node tools/check_headless_transport.mjs

# Small actual software-Vulkan diagnostic, not a hardware speed benchmark:
./tools/run.sh --headless --allow-software --no-subgroup-size \
  --total-fights 10 --runs 1 --batch-size 3 --warmup-batches 0
```

Checks cover Lua syntax/JSON/APL encoding, immutable specialization, parity acceptance rules, corrupt replay rejection, Vulkan shader compilation/SPIR-V validation, C syntax and native worker lifecycle/cancellation/error handling. Worker unit tests use synthetic process results. The separate transport test executes real Vulkan offsets/partial batches/readback. `tools/build_imgui.sh` also runs the C++ tests; offscreen UI diagnostics are described in [native/README.md](native/README.md).

The current saved five-case reference comparison remains **diagnostic-only**: **1.0424 DPS MAE**, approximate simultaneous 95% MAE bounds **0–4.1103 DPS**, unresolved input/APL/schema audits and insufficient acceptance precision. Exact optimization/frontend replay does not independently certify Forever accuracy.

```sh
# Recompute the preserved historical comparison:
node tools/compare_parity.mjs validation/evidence/headless_fast_baseline.json \
  --suite validation/evidence/headless_fast_frozen_suite.json

# Current native runner at the diagnostic suite's iteration/seed schedule:
luajit tools/run_native.lua --iterations 1000 --output artifacts/reference-native.json
node tools/compare_parity.mjs artifacts/reference-native.json
```

Parity exits 0 for accepted pass, 1 for diagnostic/fail/inconclusive, and 2 for invalid evidence. The million-fight performance schedule differs from the current reference suite; it cannot be submitted as that suite's acceptance evidence. Use native `--raw-output` with `tools/compare_variant.mjs`, `tools/check_headless_replay.mjs` or frontend `--raw-reports` with `tools/compare_imgui.mjs` for exact diagnostics.

Historical native and retired-backend evidence stays immutable under `validation/evidence/`. Generated runs/caches stay in `artifacts/`.

## Many APLs and talents

Shared-feature specialization supports varied candidates: only eligible integer mechanic settings matching across the submitted batch become constants. Differing settings remain per-candidate runtime inputs; stats and APL bytecode always remain runtime data. No candidate is forced to match another. Control-flow optimizations also apply, so shared-constant count alone does not predict speed.

**1,000 APL/talent candidates × 1,000 fights = one million individual fights.** Different actions, decision frequency, divergence and event counts may change throughput. The ~1.56M/sec result covers the frozen five-build batch. Arbitrary candidate submission and a representative diverse-search benchmark remain to be implemented; do not infer their speed from this result.

## Sources and snapshots

- `native/ui/`: thin Dear ImGui window.
- `native/frontend/`: isolated worker submission/status/cancellation and result validation.
- `tools/run_native.lua`, `tools/native_support.lua`, `tools/vulkan_probe.c`: native job preparation, content-hash caches and Vulkan/C execution.
- `sim/classes/warlock/`: specialization, preset data and frozen generated kernels/layouts.
- `sim/common/`: JSON and packed-word helpers.
- `policies/apl/`: policy compiler and VM schema.
- `data/validation/`, `validation/`: diagnostic suite, checks and preserved evidence.

The frozen combat snapshots retain their original source tags to preserve recorded hashes and exact Vulkan compiler inputs. The native wrapper translates those tags directly; no removed runtime is loaded. Future imports emit native GLSL 450. Each invocation owns one fight; local workgroups have 64 threads. Configurations use 229 words, parameters six words, fast reports 16 words and detailed reports 163 words. Invalid completion, overflow and nonfinite reports reject the evaluation.

Regenerate mechanics snapshots deliberately, then regenerate the matching suite and rerun checks/reference validation:

```sh
node tools/import_warlock.mjs /path/to/yazpads_warlock_sim SOURCE_COMMIT
node tools/import_reference_suite.mjs /path/to/yazpads_warlock_sim
node tools/generate_apl_fixtures.mjs /path/to/yazpads_warlock_sim
./tools/check.sh
```

[STATUS.md](STATUS.md) records current results, [CHECKLIST.md](CHECKLIST.md) tracks gates, [TODO.md](TODO.md) holds remaining work, and [SESSION_HANDOFF.md](SESSION_HANDOFF.md) is the next-session handoff.
