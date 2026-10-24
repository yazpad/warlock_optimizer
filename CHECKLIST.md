# Delivery checklist

Current evidence and workload are in [STATUS.md](STATUS.md). Checkboxes below separate implemented tooling, measured headless behavior, and remaining product gates.

## 1. Clean development baseline

- [x] Native LuaJIT/C/Vulkan execution with Node.js/Lua development tooling; no Python source or required commands.
- [x] Lua syntax, UI/APL, lifecycle, contract, JSON, reporting, and reference-job checks.
- [x] Native specialization checks, parity decisions/uncertainty/audit blockers, historical comparison numerics, and strict variant replay rejection checks.
- [x] Offline Vulkan shader compilation/SPIR-V validation and C compiler checks.
- [x] Content-based native executable/SPIR-V caches, isolated temporary inputs, saved input/source identities and full-run timing.
- [x] Preserve latest performance evidence and retain historical diagnostic/failed evidence.
- [ ] Rerun controlled hardware experiments with the migrated native tooling, including detailed-mode coverage.

## 2. Headless speed target

- [x] Execute one million valid frozen-workload fights on RX 5600 XT.
- [x] Reach **1.56M GPU sims/sec / 1.25M full-run sims/sec** in the latest preserved benchmark.
- [x] Pass every report-word comparison against generic in forward/reverse controls.
- [x] Retain heap40 and wave32 on supported hardware; reject unsupported subgroup requests.
- [x] Remove the large host decoding cost using C aggregation and cached readback.
- [ ] Record a fresh native warm-run distribution separately from cold compiler/pipeline startup.
- [ ] Measure mixed/edge workloads and detailed reports independently; do not generalize the frozen fast-mode result.

## 3. Next: thin Dear ImGui frontend, native compute performance

- [ ] Extract a reusable native C engine; keep CLI and GUI on the same compute implementation.
- [x] Add an experimental ImGui frontend with immutable isolated worker jobs, coarse progress and aggregate results; no per-fight UI work or frame-paced compute. The persistent C engine library is still open.
- [x] Reuse the original native runner for specialization/pipelines, wave32, heap40, 65,536-fight batches, cached readback and C aggregation. Confirm actual hardware throughput next.
- [ ] Pass full million-fight report-word replay and detailed/edge failure checks between CLI and GUI engine jobs.
- [x] Implement raw replay and alternating paired benchmark harness, with presentation/source/build identities and separate raw/aggregate-only phases.
- [ ] Run at least five hardware pairs; require ≤2% extra median GPU time and ≤5% steady overhead. Software smoke is not acceptance.
- [ ] Measure visible throttled UI and rendering-paused compute-priority modes separately; keep standalone benchmarks.
- [ ] Retire LÖVE code/patches/dependencies only after equivalent native correctness and performance coverage; preserve historical evidence.

## Historical LÖVE 12/Vulkan experiment (superseded direction)

- [x] Replace disconnected UI with a minimal benchmark workbench.
- [x] Build a project-local patched runtime with explicit subgroup-size control and opt-in GPU timestamps; preserve patch provenance.
- [x] Implement actual-width shader canary, full raw-word diagnostic replay, warmups and million-fight LÖVE benchmark; verify offline/synthetic paths.
- [x] Run patched wave32 on hardware: 1.331M GPU / 1.049M steady / 0.803M evaluation fights/sec. Further LÖVE tuning is superseded by migration.


- [x] Build pinned LÖVE 12 locally with LuaJIT/Vulkan; preserve build provenance.
- [x] Add generic/specialized fast/detailed and aggregate smoke coverage, explicit reference specialization option, and immutable option validation; synthetic execution passes.
- [x] Confirm pinned LÖVE 12.0.0 / Vulkan on AMD Radeon RX 5600 XT (RADV NAVI10); dependency lock/packaging remains future work.
- [x] Pass `--compute-smoke` and `--warlock-smoke` on real LÖVE 12/Vulkan (user-run hardware checks).
- [x] Exercise real ByteData FFI decoding with retained samples and specialized aggregate-only decoding, fast and detailed.
- [ ] Compare real FFI decoding against the ordinary unpack decoder explicitly, including invalid-report cases.
- [ ] Compare specialized/generic results in the application with different batch sizes and mixed candidates.
- [ ] Check responsiveness, cancellation, readback failure/timeout, retry, and resource cleanup on real runtime objects.
- [x] Measure LÖVE application throughput independently; preserve the result as migration evidence.

## 4. Accepted Forever accuracy

- [ ] Pin approved engine/data/mechanics provenance and resolved logical inputs.
- [ ] Audit historical APL/input/schema differences and adaptations.
- [ ] Freeze an approved matrix covering specs, pets, racials, resource edges, policies, and durations.
- [ ] Compare counters/resources/traces to explain differences, then collect sufficient independent samples.
- [ ] Establish **≤1 DPS MAE** with reported uncertainty and no unresolved acceptance blockers.

Exact optimization replay preserves generic outputs. Current five-case saved-reference comparisons are diagnostic-only and do not close this gate.

## 5. Product and search

- [ ] Product UI is deferred by user preference. Add connected product flows only after the benchmark/accuracy work and an explicit scope decision.
- [ ] Add validated saved builds/APLs, schema versions, and configuration resolution.
- [ ] Add candidate evaluation jobs with IDs, seed schedules, cancellation, and reproducible exports.
- [ ] Implement authored/evolved APL search with independent reevaluation of finalists.
- [ ] Define neural observations, action masking, weights, inference and scoring contracts; implement and measure them.
- [ ] Treat MCTS as a separate designed/measured policy workflow if pursued.
- [ ] Add another class through shared contracts, its own accuracy suite, and reproducible desktop packaging.
