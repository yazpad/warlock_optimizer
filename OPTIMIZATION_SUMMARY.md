# Optimization summary

The native Vulkan/C engine retains **~1.56M GPU fights/sec** with the thin ImGui frontend attached in rendering-paused mode. Five matched hardware pairs and full million-fight report replay passed. Native compute owns all dispatch/readback/validation/aggregation; the UI handles immutable jobs, coarse progress and final aggregates.

## Current paired hardware result

RX 5600 XT / RADV NAVI10; five fixed Warlock builds × 200,000 valid 180-second fights, seed 101, fast reports, shared features + required wave32, heap40, batch size 65,536, two discarded warmups.

| Median | Headless | ImGui, rendering paused |
| --- | ---: | ---: |
| GPU time | 638.952 ms | **638.306 ms** |
| GPU fights/sec | 1,565,063 | **1,566,647** |
| Steady evaluation | 671.069 ms | **668.115 ms** |
| Native full-run fights/sec | 1,002,581 | 996,948 |

[Full archived paired experiment](validation/evidence/latest/rx5600xt_imgui_paired/summary.json). Every one of 16 million raw report words matched across one million fights in the separate correctness phase. All GUI timing runs recorded zero UI render frames during native jobs. GPU-time ratio 0.998989 and steady-time ratio 0.995598 pass the ≤2% / ≤5% overhead gates. Small observed differences are not a speedup claim. Live rendering, other devices and diverse workloads require independent measurement.

## Retained compute optimizations

- `sim/classes/warlock/specialize.lua` specializes only shared immutable integer mechanics; stats, varying settings, timing and APL data stay runtime inputs.
- Consolidated decision/condition/cast call sites reduce inlining/code expansion. Event priorities remain ordered correctly; unused config/state fields are pruned and fast report stores are unrolled.
- Heap40 remains private. Historical smaller-heap/LDS experiments lost performance and are not active production options.
- Explicit wave32 capability checks reject unsupported requests without fallback.
- The native C engine uses cached host-visible readback and C Welford count/mean/variance/totals; raw disk reports are optional diagnostics.
- Batches of 65,536 raise utilization on the measured GPU/workload. Candidate IDs, seeds, fights and mechanics remain fixed through comparisons.

## Frontend boundary

Dear ImGui/SDL3/OpenGL3 renders the window only. The process-isolated worker runs the same LuaJIT/C/Vulkan engine as the CLI; the UI owns no compute buffers, per-fight decoder or per-GCD scheduling. Paused priority drains its announcement frame before starting work and stops issuing render/swap calls until completion. Live rendering is separately labeled and measured.

Per-job source preparation, device/pipeline setup, warmups and worker-process startup still exist. Persistent C ABI/device/pipeline/buffer extraction is the next optimization direction; preserve current exact replay and hardware performance gates through that refactor. Current full-run timing is around 1M/sec; it differs from GPU throughput and from the earlier ~1.25M/sec native orchestration result.

## Removed backend and preserved snapshots

LÖVE app/runtime code, runtime patches/build scripts, generated build dependencies and host-specific tests are removed. Historical LÖVE results remain immutable under `validation/evidence/`, including the slower 1.331M GPU / 1.049M steady / 0.803M evaluation run.

Frozen combat/suite/layout snapshots and exact wrapped Vulkan compiler inputs were preserved during cleanup. Native wrappers translate their legacy source tags directly; they load no removed runtime. Future imports emit native GLSL 450. Shared JSON encoding now lives in `sim/common/json.lua`.

Accepted Forever accuracy is still open: the saved five-case MAE 1.0424 DPS remains diagnostic-only. Neural/MCTS and diverse APL/talent throughput remain unmeasured. See [TODO.md](TODO.md), [CHECKLIST.md](CHECKLIST.md) and [native/README.md](native/README.md).
