# Current status — 2026-10-24

**ImGui preserves the ~1.56M GPU fights/sec native baseline on RX 5600 XT in compute-priority mode.** The hardware raw-replay and five-pair relative-overhead gates pass. The application now uses native/ImGui exclusively; LÖVE runtime code, build scripts/patches, generated build dependencies and backend-specific tests are removed.

## Accepted frontend performance experiment

Workload: five fixed Warlock builds × 200,000 valid 180-second fights, seed 101, fast reports, shared features + required wave32, heap40, batches of 65,536, two discarded warmup batches. Device: AMD Radeon RX 5600 XT / RADV NAVI10.

| Median | Native/headless | ImGui, rendering paused |
| --- | ---: | ---: |
| GPU fights/sec | 1,565,063 | **1,566,647** |
| GPU time | 638.952 ms | **638.306 ms** |
| Steady evaluation | 671.069 ms | **668.115 ms** |
| Native full-run fights/sec | 1,002,581 | 996,948 |
| Client-process wall | 1,006.967 ms | 1,011.407 ms |

Status: `passed-relative-overhead`, five alternating pairs, hardware rather than software diagnostics. GPU-time ratio 0.998989; steady-time ratio 0.995598. Both pass the ≤2% extra GPU time / ≤5% steady-time gates. All 16 million report words across one million fights matched in the separate replay phase. Every GUI timing run recorded zero UI rendering frames during native work. Small differences are not a speedup claim.

[Full archived experiment](validation/evidence/latest/rx5600xt_imgui_paired/summary.json) includes per-run native results, identities, timings and frontend observations. Live rendering and other workloads/devices are not certified by this result. Native wall timing remains ~1M/sec with job preparation/warmups; the historical ~1.25M native full-run result came from an earlier orchestration path and is not today's GUI rate.

## Current implementation

- Thin Dear ImGui frontend using SDL3/OpenGL3 only for UI rendering.
- Immutable jobs run in an isolated native worker process; the original LuaJIT/C/Vulkan engine owns compute, buffers, readback and C Welford aggregation.
- Required wave32, heap40, shared-feature/control-flow optimization, cached host-visible readback, optional raw reports and explicit invalid/incomplete-report rejection.
- Rendering-paused compute priority, coarse progress, process-group cancellation/cleanup, typed result validation, source/build/dependency identity and headless/live comparison modes.
- Lua/Node import/APL/validation tooling; no Python source/dependency and no LÖVE runtime dependency.

Run `./tools/run.sh`, `./tools/run.sh --benchmark` or `./tools/run.sh --headless`. `node tools/benchmark_imgui.mjs` repeats the full paired gate. `./tools/check.sh` covers the remaining native checks and C++ worker tests; `./tools/build_imgui.sh` builds/tests the frontend.

The persistent in-process C library is not extracted yet. Worker-process startup and native per-job preparation still exist; pipeline/device/buffer lifetime reuse and arbitrary candidate submission are future work.

## Remaining accuracy and coverage gates

1. **Forever accuracy:** current five-case saved-reference MAE is 1.0424 DPS with approximate simultaneous bounds 0–4.1103. Input/APL/schema audits and reference precision remain unresolved. Accepted ≤1 DPS MAE is unfinished.
2. **Coverage:** distinct mixed candidates, diverse APL/talent populations, detailed/edge cases, native worker recovery and continuous live-UI performance need broader hardware coverage.
3. **Search/product:** arbitrary candidate submission, persistence, APL evolution, neural/MCTS policies, other classes and packaging remain backlog items.

The frozen combat/suite snapshots and exact wrapped Vulkan compiler inputs were preserved during backend removal. Historical LÖVE diagnostic/build/benchmark evidence remains under `validation/evidence/`; it is not an active runtime or performance target. No further LÖVE integration is planned.
