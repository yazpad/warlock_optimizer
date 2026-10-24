# Current status — 2026-10-24

**Latest measured throughput: 1.56M GPU sims/sec; 1.25M end-to-end sims/sec.** The frozen headless performance target is met. Real LÖVE 12/Vulkan compute and Warlock smoke checks now pass on the RX 5600 XT. Accepted Forever parity, application performance, and broader runtime coverage remain open.

## Current direction: native Vulkan/C engine + thin Dear ImGui

The user chose migration from LÖVE to a thin Dear ImGui frontend. The first experimental process-isolated ImGui client is implemented; the persistent C engine library and hardware acceptance gates are still open. Retain the proven native shaders/pipelines, wave32, cached memory and C aggregation; keep simulation independent of UI frames. Initial acceptance gates are full report replay, no measurable regression, ≤2% extra paired median GPU time and ≤5% steady evaluation overhead. Measure visible and rendering-paused modes separately. See [TODO.md](TODO.md) for the ordered plan.

The user ran the actual patched LÖVE benchmark: median **1,331,415 GPU / 1,049,484 steady / 802,978 evaluation fights/sec**. The saved [hardware result](validation/evidence/latest/love12_wave32_benchmark.json) confirms required/canary wave32. Cached source preparation still takes ~176 ms/job and LuaJIT summary ~185 ms/million fights, compared with ~12 ms native C aggregation. The GPU difference from historical native is real in these saved runs but its cause is unisolated; back-to-back controls are required before assigning it to compilation or GPU clocks/load.

LÖVE remains available as a historical/diagnostic comparison during migration. Stop expanding its integration; retire it only after the native frontend passes equivalent correctness/performance gates. [Runtime patch provenance](validation/evidence/latest/love12_wave32_build_info.txt) is preserved. The disconnected product tabs were already removed; the current view is a minimal benchmark workbench.

## First native frontend slice

`./tools/run_imgui.sh` now opens an experimental minimal Dear ImGui window. GUI and `--headless` share a renderer-independent native job interface that launches the original LuaJIT/C/Vulkan runner. The compute source, SPIR-V path, memory strategy and C aggregation are reused. This is an isolated process client, not the planned persistent in-process library.

The frontend builds, its SDL3/OpenGL3 window renders in offscreen software mode, and process lifecycle/rejection/cancellation tests pass. A 10-fight real software-Vulkan comparison matched every report word between headless and GUI modes; paused mode recorded zero UI render frames during work. `node tools/benchmark_imgui.mjs` implements the raw replay gate plus alternating timing pairs. **No new hardware throughput claim is made**; run the ≥5-pair hardware harness and check the standalone native baseline before acceptance. Details: [native frontend](native/README.md).

## Latest measured workload

AMD Radeon RX 5600 XT, RADV NAVI10. Five frozen Warlock baseline scenarios × 200,000 fights; one million valid 180-second fights, seed 101, fast reports, batch size 65,536, two discarded warmup batches, 40-entry event heap.

The latest preserved six-run forward/reverse experiment reports:

| Variant | Median GPU time | GPU sims/sec | Median full-run time | Full-run sims/sec |
| --- | ---: | ---: | ---: | ---: |
| Generic | 1,866.225 ms | 536k | 2,210.770 ms | 452k |
| Shared features, wave64 | 792.252 ms | 1.26M | 973.339 ms | 1.03M |
| Shared features, wave32 | 639.844 ms | **1.56M** | 797.485 ms | **1.25M** |

All five replay comparisons passed **every word of one million fast reports** (16 million words each). Evidence: [latest benchmark](validation/evidence/latest/rx5600xt_wave32_abc_cba.json). This experiment used the earlier host orchestrator; the source is now migrated to LuaJIT/Node.js. Its timing remains historical evidence, not a fresh measurement of the updated tooling.

A separate native LuaJIT/C run preserved under [native probe evidence](validation/evidence/latest/rx5600xt_native_million_probe.json) records 640.074 ms GPU time and 643.973 ms dispatch/readback, with five 200,000-fight summaries. Its historical file does not contain outer Lua wall time; the previous reported ~1.06M/sec native full-run figure is not independently recoverable from that file. New native runs save outer timing and input/source metadata.

## Retained implementation

- Shared immutable integer mechanic specialization; APL bytecode and varying inputs remain runtime data.
- Consolidated shader control flow, dead-field pruning, and packed event priorities.
- Heap40 retained. Heap24 and LDS experiments are not retained performance paths.
- Wave32 on supported hardware; unsupported requests fail explicitly.
- Cached host-visible readback and C Welford validation/summary replace expensive per-fight host decoding.
- Native LuaJIT/C/Vulkan runner; Node.js development tooling. No Python source or required commands remain.
- LÖVE host specialization and FFI report decoding pass the hardware smoke checks, including generic/specialized fast/detailed common-field replay and specialized aggregate decoding.

## Open acceptance gates

LÖVE 12 is now built from pinned revision `fc42127510659651ee692ab820992fd978fae92c`, with LuaJIT and Vulkan. The user ran `--compute-smoke` and `--warlock-smoke` successfully on AMD Radeon RX 5600 XT / RADV NAVI10. This covers reflection/buffer dispatch/readback, generic/specialized fast/detailed common-field replay, specialized aggregate decoding, batching/candidate mapping, and insufficient-event-budget rejection. Live cancellation/timeout/retry and diverse-candidate coverage remain separate checks.

The specialized reference export contains **5,000 completed detailed fights**, 1,000 per frozen scenario, and explicitly records 20 shared mechanic constants. Its saved comparison reproduces **1.0424 DPS MAE** and approximate bounds **0–4.1103 DPS**: diagnostic-only, with suite audits still open. The reference job used batch size 256, included ~811 ms compilation, and was frame-driven; its ~4,501 fights/sec full-run rate is not a throughput benchmark comparable to the million-fight native test.

Preserved evidence: [LÖVE reference export](validation/evidence/latest/love12_specialized_reference.json), [comparison](validation/evidence/latest/love12_specialized_comparison.json), and [build provenance](validation/evidence/latest/love12_build_info.txt). The smoke pass is reported from the user's terminal; the export and comparison were inspected locally. `conf.lua` now uses `t.graphics.renderers` to remove the reported deprecated-field warning.

1. **Forever parity:** initial five-case diagnostic MAE 1.0424 DPS; approximate simultaneous 95% MAE bounds 0–4.1103. Saved reference means, unresolved input/APL/schema audits, and sample precision prevent acceptance. An approved ≤1 DPS MAE suite remains unfinished.
2. **LÖVE integration:** basic compute and Warlock integration now pass on hardware. Patched wave32 hardware throughput has now been measured and saved. Further LÖVE expansion is superseded by the native-engine/ImGui plan; live lifecycle and diverse-candidate coverage must transfer to the new engine/frontend.
3. **Coverage:** broader hardware detailed/edge/mixed-policy coverage and cold-start measurements remain open. The retained throughput result covers the frozen fast workload only.
4. **Product work:** build persistence/resolution, search/neuroevolution, additional classes, and packaging remain backlog items.

## Cleanup validation

The development suite retains UI/APL/lifecycle/contract checks, native specialization guards, parity acceptance and historical numerical checks, strict replay corruption rejection, shader compilation/SPIR-V validation, and C syntax checks. Actual software-Vulkan transport and small native generic/specialized fast/detailed replay are separate execution checks. No new hardware performance claim is made during cleanup.

See [CHECKLIST.md](CHECKLIST.md) and [SESSION_HANDOFF.md](SESSION_HANDOFF.md) for the next steps. Older files directly under `validation/evidence/` are historical results and must keep their original hashes and timings.
