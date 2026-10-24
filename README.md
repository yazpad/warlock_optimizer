# Fastsim

**Experimental thin Dear ImGui frontend is now implemented.** It submits isolated jobs to the existing native Vulkan/C runner, so the UI owns no simulation buffers, dispatch or report decoding. Rendering pauses during jobs by default. The planned persistent in-process engine library is still a separate task.

`./tools/run_imgui.sh` opens the new frontend. `./tools/run_imgui.sh --benchmark` runs the native workload with the GUI attached and exits; `--headless` uses the same worker without a window. [Native frontend details](native/README.md) describe architecture, dependencies, timing scope and remaining acceptance gates. LÖVE remains a legacy comparison path during migration.

The measured native target on RX 5600 XT / RADV NAVI10 is **1.56M GPU sims/sec** for five frozen Warlock builds × 200,000 fights, 180-second duration, seed 101, fast reports, heap40, batches of 65,536 and two warmups. Historical native full-run throughput reached 1.25M/sec; a later user-run native evaluation reported 1.01M/sec with the same ~1.56M GPU rate. The actual LÖVE wave32 median is **1.331M GPU / 1.049M steady / 0.803M evaluation fights/sec**. This result motivates reusing the native compute path behind a thin frontend. The paths have different timing scopes and have not been compared in a matched alternating experiment.

[STATUS.md](STATUS.md) records evidence, [SESSION_HANDOFF.md](SESSION_HANDOFF.md) has next steps, [CHECKLIST.md](CHECKLIST.md) tracks gates, and [TODO.md](TODO.md) holds future work. [OPTIMIZATION_SUMMARY.md](OPTIMIZATION_SUMMARY.md) describes retained optimizations.

## Thin ImGui frontend

```sh
./tools/build_imgui.sh
./tools/run_imgui.sh
./tools/run_imgui.sh --benchmark
./tools/run_imgui.sh --headless

# Full raw replay gate, followed by five paired hardware timing comparisons:
node tools/benchmark_imgui.mjs
```

The frontend uses SDL3/OpenGL3 only for UI rendering; simulation stays in the unchanged native Vulkan/C compute path with wave32 and C aggregation. Default compute-priority mode stops UI rendering during jobs while keeping input/cancellation handling alive. Optional `--live-ui` is measured separately at approximately 10 Hz. Jobs retain native JSON, progress/error logs and a summary under `artifacts/imgui/`. Headless/paused/live labels, UI frame counts and source identities are retained.

The build and offscreen UI smoke pass. A small software-Vulkan test passed exact frontend/headless replay with zero paused-mode UI frames during native work. **Hardware ~1.56M/sec and relative-overhead acceptance remain unverified.** The paired harness requires ≥5 hardware pairs and ≤2% median GPU-time / ≤5% steady-evaluation overhead; software or shorter runs remain diagnostic-only. The current UI runs the fixed suite; arbitrary configuration/APL editing and persistent engine-library extraction are deferred.

## Legacy LÖVE benchmark (retained during migration)

```sh
# Rebuild the local runtime with the checked-in subgroup/timestamp patch.
./tools/build_love12.sh

# A million fights per run, specialized wave32; three measured runs.
./tools/run.sh --benchmark

# Optional controls, each with its own result filename:
./tools/run.sh --benchmark --generic --output love-generic-wave32.json
./tools/run.sh --benchmark --no-subgroup-size --output love-specialized-default.json
./tools/run.sh --benchmark --generic --no-subgroup-size --output love-generic-default.json
```

The benchmark:

1. Reads `gl_SubgroupSize` from a small actual compute shader and checks the requested width.
2. Runs diagnostic generic/specialized/default-width/required-width fast and detailed replay checks. **Every raw report word** must match the same-mode generic baseline; fast/detailed common fields and aggregate statistics must agree. These preflight checks use the small smoke fixture, not a million-fight replay.
3. Evaluates the frozen million-fight workload through **LÖVE buffers, compute dispatch, readback and LuaJIT report decoding**. It does not call the standalone C transport.
4. Discards two warmup dispatches per run, validates every measured report, checks per-candidate sample counts, and compares repeated aggregate results.
5. Saves timings, candidate summaries, device, suite/kernel identity and observed subgroup width to `love-benchmark.json` in LÖVE's save directory, normally `~/.local/share/love/fastsim/`.

Console output separates:

- **GPU:** Vulkan timestamp intervals around measured compute dispatches; compare this with the native **1.56M/sec** target.
- **Evaluation:** per-job wall time including request setup, specialization/shader creation, warmups, readback and LuaJIT summary. Excludes process/window startup and preflight.
- **Steady:** measured evaluation time after setup/compilation/warmups, including dispatch/readback/decoding.

These are different measurements. A matching GPU rate does not establish matching overall throughput. The first run may incur pipeline creation; per-run compilation and decoding times are retained. Failed replay, missing timestamps, invalid reports and unsupported explicit widths reject the run. `love-benchmark.json` is overwritten; use distinct filenames to retain controls. Default width is driver-selected, not assumed wave64. The canary observes its own pipeline; when no width is required, its width does not establish the combat kernel’s selected width. With a required width, the same Vulkan requirement is enforced on every benchmark pipeline.

Use `--runs 1`, `--total-fights N`, `--batch-size N`, or `--warmup-batches N` for diagnostics; changed workloads are not directly comparable to the target. `--subgroup-size 32` is the default, or request another supported width. `--allow-software` permits software Vulkan diagnostics; they are not hardware-performance evidence. The benchmark uses synchronous readback waits so it can submit consecutive batches without UI frame pacing; Escape is handled between batches, not during a GPU wait.

For the small view, run `./tools/run.sh`; **B** runs specialized wave32, **G** runs generic wave32, and **Escape** quits. The CLI benchmark exits after saving its results. No full product UI or search algorithm is currently connected.

## Build LÖVE 12 locally

Dependencies: Lua/LuaJIT, Node.js 22+, C/C++ toolchain, CMake, Ninja, Vulkan loader/headers, `glslangValidator`, `spirv-val`, `sha256sum`, and a Vulkan driver. Node.js tools have no npm dependencies. There is no Python source or required command.

The build helper expects the clean LÖVE checkout at `/home/aaron-ma/love`, pinned to `fc42127510659651ee692ab820992fd978fae92c`. Use `--source /path/to/love` to override its location. SDL3 >=3.1.3, FreeType and LuaJIT development files must be available.

```sh
# If HarfBuzz/audio/codec development dependencies are missing (Ubuntu/Debian):
./tools/build_love12.sh --download-deps

# Existing dependencies:
./tools/build_love12.sh
```

The helper extracts missing dependencies locally, creates a project-local copy of the pinned source and applies [the Fastsim patch](tools/patches/love12-fastsim.patch). The upstream checkout stays unchanged. Build products/dependencies/logs live under ignored `artifacts/love12/`. Set `LOVE_BUILD_JOBS` to change the default four compiler jobs. `--configure-only` checks configuration without building.

The installed executable is `artifacts/love12/install/bin/love`; `tools/run.sh` selects it automatically. `LOVE_BIN=/path/to/love` overrides that choice. `build-info.txt` records the source revision and patch hash. The patch adds `requiredsubgroupsize` to compute shader options, queries/enables Vulkan subgroup-size control, rejects unsupported widths, and adds the custom `love.graphics.fastsimComputeTiming` extension. GPU timestamps are optional instrumentation activated only during timed evaluation. These are **Fastsim changes, not upstream LÖVE APIs**. An unpatched runtime cannot run the benchmark or explicit-width requests silently.

LuaJIT is explicitly enabled; the upstream Linux source already enables Vulkan. Shared-feature specialization is a simulator request option. It is independent of wave width. The patched runtime was built and the user ran its hardware benchmark successfully. Further LÖVE performance development is superseded by the native-engine/ImGui migration.

## Checks and reference accuracy

```sh
./tools/check.sh
./tools/doctor.sh
./tools/run.sh --compute-smoke
./tools/run.sh --warlock-smoke
./tools/run.sh --warlock-smoke --subgroup-size 32
./tools/run.sh --parity-reference --specialize-features
```

The offline suite checks the minimal workbench, Lua syntax, APL compiler, contracts/statistics, lifecycle failures, specialization, replay/acceptance rejection, benchmark orchestration with synthetic transport, shader compilation/SPIR-V validation, and C syntax. Mocked timings are not measurements. The real smoke command checks readback, candidate mapping, full raw-word replay, aggregate decoding and event-budget rejection; an explicit subgroup option adds width-specific generic/specialized replay.

Reference exports write `warlock-parity.json` in the LÖVE save directory. Copy that file before exporting another variant if retaining both. Compare with:

```sh
node tools/compare_parity.mjs ~/.local/share/love/fastsim/warlock-parity.json
```

The current five-case suite remains **diagnostic-only**, including the actual LÖVE export: **1.0424 DPS MAE**, approximate simultaneous 95% MAE bounds **0–4.1103 DPS**, unresolved input/APL/schema audits, and insufficient precision for acceptance. Exact optimization replay preserves generic results; it does not independently certify Forever accuracy. Reference comparison exits 0 for accepted pass, 1 for diagnostic/fail/inconclusive, and 2 for invalid evidence.

Preserved evidence lives in `validation/evidence/`; keep historical files/hashes immutable. Generated runs/build caches live in `artifacts/`. The million-fight benchmark's iteration count differs from the current reference suite; it is not a valid acceptance comparison for that suite.

## Native baseline and headless diagnostics

```sh
./tools/benchmark_gpu.sh
./tools/benchmark_wave32.sh --output-dir artifacts/wave32-next
./tools/benchmark_specialization.sh --output-dir artifacts/specialization-next
node tools/check_headless_transport.mjs
luajit tools/run_native.lua --allow-software --no-subgroup-size \
  --total-fights 10 --batch-size 3 --warmup-batches 0 --output /tmp/fastsim-smoke.json
```

The native command uses LuaJIT/C/Vulkan with content-hashed executable/SPIR-V caches. Controlled native experiments compare every million-fight report word against generic and include raw-output overhead in full-run timings. The native CLI still restricts explicit subgroup overrides to specialization; **the LÖVE benchmark supports generic wave32 as an independent control**.

Replay tools for saved native raw reports:

```sh
node tools/compare_variant.mjs generic.json generic.bin variant.json variant.bin
node tools/check_headless_replay.mjs fast.json fast.bin detailed.json detailed.bin
```

## Many APLs and talents

Shared-feature specialization supports varied candidates. Only eligible integer mechanic settings identical across the submitted batch become constants. Differing talents/settings stay per-candidate runtime inputs; no candidate is forced to match another. Stats and APL bytecode always remain runtime data. Control-flow optimizations also apply, so shared-constant count alone does not predict speed.

The packed contract supports up to 1,024 candidates. **1,000 APL/talent candidates × 1,000 fights = one million individual fights**, not one million candidate evaluations. Fewer common mechanics, different decision frequency/actions, divergence and event counts may change throughput. The **1.56M/sec result is for the frozen five-build batch**, not a measured diverse search population. Build and measure that representative workload before making search-speed claims.

## Source and layout

`main.lua` is the minimal workbench; `benchmarks/love.lua` runs the LÖVE benchmark. `sim/gpu/warlock_runner.lua` handles application dispatch/reporting; `contracts/packed_evaluation.lua` validates requests. Requests select `specializeFeatures`, optional `subgroupSize`, and `blockingReadback`; normal asynchronous execution remains available. `retainRawReports` is bounded diagnostic-only retention.

`sim/classes/warlock/` holds presets, specialization, reporting and generated kernels; `policies/apl/` retains policy compilation. Each invocation owns one fight. Kernels use 64 threads/workgroup and three buffers: 229-word configurations, six parameter words, and 16-word fast or 163-word detailed reports. Invalid completion, overflow, nonfinite values and event exhaustion are failures.

Regenerate snapshots deliberately, then rerun checks and accuracy validation:

```sh
node tools/import_warlock.mjs /path/to/yazpads_warlock_sim SOURCE_COMMIT
node tools/import_reference_suite.mjs /path/to/yazpads_warlock_sim
node tools/generate_apl_fixtures.mjs /path/to/yazpads_warlock_sim
./tools/check.sh
```

Importers record source hashes. Generated fixtures are not accepted reference cases. The old product UI is deferred; search, neural policies, MCTS, persistence, additional classes and packaging remain future work.
# warlock_optimizer
