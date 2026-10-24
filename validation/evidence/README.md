# Preserved execution evidence

Files here retain their original source hashes, schemas, results, and timings. They are historical evidence, not regenerated output from the latest tooling.

## Latest performance evidence

- `latest/rx5600xt_wave32_abc_cba.json`: latest six-run hardware experiment, **1.56M GPU / 1.25M end-to-end sims/sec**, generic/shared-wave64/shared-wave32 forward/reverse order, five million-fight report-word replay gates. Original measurement filenames refer to generated `artifacts/` paths; the summary embeds timings, provenance, compiler statistics and gates.
- `latest/rx5600xt_native_million_probe.json`: native million-fight C probe JSON with device/timings and candidate summaries. Outer Lua wall time was not saved in this historical file.

These copies are distinct from the earlier `rx5600xt_wave32_abc_cba.json` directly in this directory, which records the older ~382k GPU/sec result. Original evidence is not overwritten. The latest result covers the frozen five-scenario, fast-report, million-fight workload only.

## LÖVE 12 hardware integration

- `latest/love12_build_info.txt`: pinned source revision, LuaJIT setting, upstream Linux Vulkan support and build-source hashes.
- `latest/love12_specialized_reference.json`: actual LÖVE 12.0.0/Vulkan RX 5600 XT detailed export, shared-feature specialization enabled, five cases × 1,000 valid fights. This is a small diagnostic reference job, not a throughput benchmark.
- `latest/love12_specialized_comparison.json`: diagnostic-only 1.0424 DPS MAE with approximate bounds 0–4.1103 and unresolved acceptance blockers.

The user also reported passing compute and generic/specialized Warlock smoke checks in the hardware terminal. That establishes basic LÖVE execution; broader live lifecycle coverage and application throughput remain open.

## Historical diagnostics

- `headless_fast_baseline.json`: 5,000 real software-Vulkan fast fights; metadata and aggregate reports.
- `headless_fast_frozen_suite.json`: exact original reference snapshot.
- `headless_fast_comparison.json`: diagnostic-only 1.0424 DPS MAE and approximate sampling bounds against saved Forever means; reference engine not rerun.
- `archived_webgl_mean_comparison.json`: saved mean comparison, not per-fight source replay.
- `fast_detailed_replay.json`: original failed replay exposing corrupt/repeated detailed tail fields.
- `fast_detailed_replay_fixed.json`: corrected 325-fight replay, 24 common fields, differing batch sizes.
- `headless_fast_replay_stats.json`, `headless_detailed_replay_stats.json`: 65-fight-per-case replay aggregates; do not compare these directly with the earlier 1,000-fight-per-case diagnostic means as a mechanics regression.
- Other specialization, heap, subgroup and hardware files record their named experiments, including rejected variants and failure handling.

Recompute the historical diagnostic with current tooling:

```sh
node tools/compare_parity.mjs validation/evidence/headless_fast_baseline.json \
  --suite validation/evidence/headless_fast_frozen_suite.json
```

This exits 1 because diagnostic suites cannot pass acceptance. Raw binaries are generated artifacts. Use native `--raw-output`, `tools/compare_variant.mjs`, and `tools/check_headless_replay.mjs` for new replay evidence. No saved evidence here certifies accepted ≤1 DPS MAE; the named LÖVE export establishes actual runtime execution within its stated diagnostic scope.

`latest/love12_wave32_build_info.txt` records the newly built Fastsim subgroup/timestamp runtime patch hash. It is build provenance; the actual hardware run is saved separately below. The earlier `love12_*` smoke/reference evidence predates that patch. New benchmark results belong in a distinct file and must not relabel the historical native 1.56M/sec measurement as a LÖVE result.

`latest/love12_wave32_benchmark.json` now preserves the actual hardware run: median **1.331M GPU / 1.049M steady / 0.803M evaluation fights/sec**, required and canary-observed width 32. It is a historical LÖVE result and a migration comparison, not the acceptance target for the planned native-engine/ImGui frontend.

`latest/imgui_native_software_smoke.json` records the first actual process-client/offscreen ImGui smoke: ten software-Vulkan fights, exact headless/GUI raw report replay, zero UI rendering frames during paused native work, native engine/runner identity and frontend source/build identity. Required wave32 was disabled for the software device. This is behavior/correctness smoke evidence only; it does not certify hardware wave32 throughput, million-fight replay, or the ≥5-pair hardware performance gate.
