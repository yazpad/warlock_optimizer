# Remaining work

**Decision: migrate from LÖVE to a thin Dear ImGui frontend over the existing native Vulkan/C engine.** Retain native GPU performance as a release gate. The first process-isolated ImGui job client is implemented; persistent native library extraction and hardware performance acceptance remain next steps.

The fixed RX 5600 XT benchmark reached approximately **1.56M GPU fights/sec** natively. The actual LÖVE wave32 run reached **1.331M GPU / 1.049M steady / 0.803M evaluation fights/sec**. That experiment is preserved in [the hardware result](validation/evidence/latest/love12_wave32_benchmark.json). Accuracy acceptance remains separate and unfinished.

## First runnable frontend slice (implemented)

- [x] Add a minimal Dear ImGui client with immutable native jobs, coarse progress, candidate summaries, timing and cancellation. SDL3/OpenGL3 renders the UI only; the existing native Vulkan/C engine still performs all compute/reduction.
- [x] Isolate the existing exit-on-error native runner in a worker process group; handle failure/corrupt results, signal cancellation, cleanup and evidence preservation. The C ABI extraction below is not yet done.
- [x] Default to rendering-paused compute priority, draining the announcement frame before launch. Keep the event loop/cancel path alive; measure optional 10 Hz live rendering separately.
- [x] Pin/vendor unmodified ImGui sources/license/hashes; build and render the window offscreen. Pass CTest process-boundary tests and small actual software-Vulkan raw replay, including zero paused UI frames during jobs.
- [x] Add headless/GUI controls and a raw-replay-first paired performance harness. Hardware gates remain unchecked; do not claim 1.56M/sec through the frontend yet.

## 1. Extract the existing native engine

- [ ] Refactor the proven `tools/vulkan_probe.c` execution path into a reusable native C library with a small C ABI. Keep the native CLI as a frontend to the same engine; avoid separate GUI/CLI simulation implementations.
- [ ] Preserve existing shaders/SPIR-V compilation, specialization, explicit wave32 checks, heap40, batch size 65,536, seed schedules, completion/failure validation, host-cached readback and C Welford aggregation. Preserve generic/default-width controls.
- [ ] Keep setup, shader generation and compilation outside repeated evaluation work. Reuse cached pipelines and buffers; derive a cheap source/feature signature before rebuilding specialized shader text.
- [ ] Add versioned request/result boundaries for packed candidates, runtime APLs, seed/iterations, objective, report mode and optional diagnostic output. Keep Lua/Node tools for generation/import/validation where useful; no Python dependency.
- [ ] Separate initialization/compilation, discarded warmups, measured GPU dispatch, transfer, C aggregation, steady evaluation and process startup timing.

## 2. Add a genuinely thin Dear ImGui frontend

- [x] Build the first minimal Dear ImGui window with pinned sources/hashes/license. SDL3/OpenGL3 renders UI only; the existing native Vulkan worker remains independent. Hardware performance gates and final dependency packaging remain open.
- [x] Limit the prototype to job settings/submit/cancel, progress, completed fixed-suite aggregates, timings and failures. Arbitrary candidate editing, dashboards and editors stay deferred.
- [x] Submit immutable jobs to a process-isolated native worker; compute resources/dispatch/readback/aggregation remain in the existing engine. UI consumes coarse progress and final aggregates. Persistent C ABI/device reuse is still open.
- [x] Keep per-fight objects, event/GCD callbacks, raw report decoding and shader compilation out of UI code. Diagnostic raw reports are explicit native disk outputs, used separately from aggregate-only timing.
- [ ] Reuse the exact native compute pipelines and memory strategy. Rendering must not cause reuploads, recreate compute resources, introduce device-wide waits or put frame/vsync pacing between compute batches.
- [ ] Throttle progress publication/UI redraws independently of compute. Offer a clearly labeled compute-priority mode that pauses rendering during timed work if the shared GPU would otherwise lose throughput; continue event/cancellation handling on the appropriate thread.
- [ ] Keep renderer/compute resource ownership and synchronization explicit. Measure shared-GPU contention; a different UI library alone does not guarantee free rendering.

## 3. Performance acceptance: preserve the native baseline

**Goal: no measurable compute regression from attaching the frontend.** Historical 1.56M/sec is the target for the frozen workload, not a promise across devices or diverse search populations.

- [ ] First rerun the native baseline on the same GPU/driver and current source. Use the identical five-scenario × 200,000-fight, 180-second, seed-101, fast, heap40, wave32 workload with batches of 65,536 and two discarded warmups.
- [ ] Run at least five alternating paired native/headless and ImGui-attached measurements with matched inputs, kernels, seeds, warmup/cache state and timing definitions. Record clocks/load where available; retain all samples, medians and spread.
- [ ] Require full report-word replay between CLI and GUI engine jobs on the frozen million-fight workload, plus detailed/edge smoke coverage. Failed/incomplete/overflow/nonfinite fights must never contribute accepted throughput.
- [ ] Verify subgroup capability enforcement and actual-width canary. Unsupported explicit wave32 must fail without fallback.
- [ ] Compare GPU timestamps first. Initial regression ceiling: GUI-attached median GPU time no more than **2% above** paired native time; investigate any repeatable loss even below that ceiling. Do not accept the earlier ~15% LÖVE GPU-rate loss as the new baseline.
- [ ] Compare matched steady full-evaluation time too: no more than **5% overhead** from attaching the frontend, while preserving C validation/reduction. Report cold startup, compilation and warmups separately rather than hiding them.
- [ ] Measure native/headless, visible throttled UI, and rendering-paused compute-priority modes separately. Label each result; do not use a paused-UI result as proof that continuous rendering is free.
- [ ] If either gate fails, profile/fix the interface, scheduling, memory copies, shader/code generation or rendering contention before expanding the UI. Retain aggregate-only operation and the standalone native benchmark.

## 4. Cut over and clean up

- [ ] Make the new native frontend/build/run commands the primary workflow only after correctness and performance gates pass.
- [ ] Preserve LÖVE benchmark/reference/build evidence as historical artifacts. Stop investing in LÖVE-specific performance patches; keep the existing path available for comparison during migration.
- [ ] Once native frontend coverage replaces useful LÖVE diagnostics, remove obsolete LÖVE runtime patches, app code, build scripts and dependencies. Preserve APL compiler, generated kernels/layouts, reference suites, comparators and native validation.
- [ ] Rewrite startup, dependency, benchmark and handoff documentation around the final implementation. Do not label planned commands as available.

## 5. Accuracy and robustness

- [ ] Audit/pin Forever revision, generated data, mechanics, resolved stats/talents/race/pet/buffs/equipment, APLs, encounter and durations. Resolve historical input/APL/schema adaptations.
- [ ] Freeze the approved acceptance matrix; compare counters/resources/event ordering/RNG continuation/traces and collect sufficient independent samples to resolve **≤1 DPS MAE**. Current five-case 1.0424 DPS MAE remains diagnostic-only.
- [ ] Validate statistics across fast/detailed, resource edges, zero damage, NaNs, overflow and event-budget exhaustion. Test native worker cancellation, timeout/device failure, retry and cleanup without accepting partial results.
- [ ] Keep source, shader, input, engine, driver and schema identities in benchmark/reference artifacts. Keep historical evidence immutable.

## 6. Later search and product work

- [ ] Build a representative **1,000 distinct APL/talent candidates × 1,000 fights** benchmark. Mixed candidates are supported, but their throughput is unmeasured. Shared specialization keeps differing settings as runtime data; APLs/stats always remain runtime inputs.
- [ ] Add generic wave32 as an independent native control, separate from specialization. It exists in the LÖVE experiment but the current native CLI restricts explicit widths to specialization.
- [ ] Add reproducible APL evolution with held-out seeds and independent finalist reevaluation; define neural/MCTS contracts before implementation. No policy performance forecast counts as a measurement.
- [ ] Add validated persistence/config resolution, additional classes with independent accuracy suites, and packaging after the current engine/frontend goals.
