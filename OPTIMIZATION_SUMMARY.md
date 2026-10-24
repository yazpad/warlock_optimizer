# Optimization summary

The latest measured RX 5600 XT / RADV NAVI10 result is **1.56M GPU sims/sec and 1.25M end-to-end sims/sec** for the frozen fast workload. The headless speed target is met. Accepted Forever accuracy remains open. Basic LÖVE 12/Vulkan hardware integration now passes; application performance and broader runtime coverage are separate open gates.

## Evidence and workload

Five frozen baseline Warlock scenarios × 200,000 fights, 180-second duration, seed 101, 40-entry heap, batch size 65,536, two discarded warmup batches. The preserved generic/shared-wave64/shared-wave32 forward/reverse experiment passes all 16 million report words in each of five comparisons against generic.

| Variant | Median GPU time | Median dispatch/readback | Median full-run time |
| --- | ---: | ---: | ---: |
| Generic | 1,866.225 ms | 1,885.669 ms | 2,210.770 ms |
| Shared features, wave64 | 792.252 ms | 806.129 ms | 973.339 ms |
| Shared features, wave32 | **639.844 ms** | **651.448 ms** | **797.485 ms** |

[Preserved latest experiment](validation/evidence/latest/rx5600xt_wave32_abc_cba.json). These are two measurements per variant, not a broad performance distribution. Initial baseline timings at smaller batches and with old decoding are retained in historical evidence; they are not the simultaneous controls above.

The separate [native C probe result](validation/evidence/latest/rx5600xt_native_million_probe.json) records 640.074 ms GPU, 643.973 ms dispatch/readback, and 12.166 ms C decode. Its original outer Lua wall time was printed but not saved. Current tooling saves that timing and provenance; rerun hardware tests to measure the migrated path.

## Retained shader changes

`sim/classes/warlock/specialize.lua` specializes integer mechanics shared across submitted candidates. Stats, varying flags, clocks and APL data remain runtime inputs.

- Consolidate `decide()`, condition evaluation, and cast dispatch call sites to reduce compiler inlining/code expansion.
- Pack event tie-break priority into event data while preserving ordering.
- Prune unused config/state fields; unroll fast report stores.
- Retain the private 40-entry event heap. The smaller-heap and LDS experiments are historical rejected performance options.
- Request wave32 only where supported; fail unsupported requests without fallback.

Exact replay is the guard for optimization changes, including future policy/feature combinations. Passing the frozen baseline does not establish general policy coverage or independent engine fidelity.

## Retained host changes

`tools/vulkan_probe.c` validates reports and computes count/mean/variance/totals in C. Cached host-visible readback removes the large uncached-memory decoding stall. Full raw reports are optional diagnostics rather than required disk output. Increasing batch size to 65,536 raises device utilization on the benchmark workload.

`tools/run_native.lua` uses LuaJIT/C/Vulkan directly. C and SPIR-V caches use SHA-256 content identities. Inputs are isolated in per-run temporary directories. Saved results include suite/input/kernel identity, candidate summaries, device/probe information, and native full-run/build/shader/pipeline timings. Node.js provides benchmark orchestration, comparisons, validation, and importers; no Python source or required commands remain.

The LÖVE host has Lua specialization and an FFI batch report decoder. Generic/specialized fast/detailed replay and specialized aggregate ByteData decoding now pass the hardware LÖVE smoke test. The actual LÖVE wave32 median is 1.331M GPU / 1.049M steady / 0.803M evaluation fights/sec; this is separate from the native baseline.

## LÖVE benchmark transfer

The pinned LÖVE 12 runtime now builds with a project-local Fastsim patch for required subgroup width and compute GPU timestamps. `./tools/run.sh --benchmark` checks actual subgroup width, preflights exact generic/variant raw report replay, and runs three million-fight evaluations through LÖVE's own dispatch/readback and LuaJIT decoding. Generic wave32 is an independent control. The disconnected UI was replaced with a minimal workbench.

The user ran the patched runtime on hardware; its result is preserved at `validation/evidence/latest/love12_wave32_benchmark.json`. The native C readback/reduction path and LÖVE's staging-copy/LuaJIT path differ. Compare GPU timestamps with the native 1.56M/sec figure; compare steady and full evaluation wall rates separately. Process/window startup and diagnostic preflight are outside the per-evaluation timer. See README for commands and exact timing scope.

## Next work

Migrate to a thin Dear ImGui frontend over the native Vulkan/C engine, preserving C aggregation and independent compute scheduling. Require exact replay and paired performance gates before expanding UI functionality; see TODO.md. Stop expanding LÖVE-specific integration and preserve its results as historical evidence. Finish the approved Forever input/APL/accuracy audit separately. Continue kernel/reduction work only when new profiling or workloads justify it. No neural inference, neuroevolution, or MCTS throughput has been established; earlier numerical forecasts must not be treated as benchmark results.

Commands and dependencies are in [README.md](README.md); delivery gates are in [CHECKLIST.md](CHECKLIST.md).
