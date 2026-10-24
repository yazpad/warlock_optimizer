# Preserved execution evidence

Files here keep their original schemas, source/input hashes, results and timing scopes. They are immutable records, not regenerated output from today's tooling. Generated jobs and raw diagnostics live in ignored `artifacts/`.

## Current native/ImGui hardware acceptance

`latest/rx5600xt_imgui_paired/summary.json` and its child job files archive the complete RX 5600 XT hardware experiment:

- Five alternating headless/GUI timing pairs, rendering paused during native work.
- Median GPU rate **1,565,063 headless / 1,566,647 GUI fights/sec**.
- GPU-time ratio **0.998989**, steady-time ratio **0.995598**, passing the ≤2% / ≤5% overhead ceilings.
- Exact replay across **one million fights / 16 million raw report words** before timing.
- Zero frontend render frames in every paused-mode timing run.
- Original native results and engine/runner/frontend source/build/device identities retained alongside the summary.

Raw replay binaries were removed by the experiment after validation; archived JSON retains the gate counts/results. Small timing differences are not a speedup claim. This certifies the frozen five-build workload and paused UI on the tested GPU, not continuous rendering, other devices or diverse search populations.

`latest/imgui_native_software_smoke.json` is the earlier ten-fight software-Vulkan/offscreen frontend smoke. It checks report agreement and frame accounting, with wave32 disabled for the software device. It is not hardware speed evidence.

## Earlier native performance evidence

- `latest/rx5600xt_wave32_abc_cba.json`: six-run generic/shared-wave64/shared-wave32 experiment, approximately 1.56M GPU / 1.25M full-run fights/sec, five million-fight raw replay gates. Full-run timing used an earlier orchestrator.
- `latest/rx5600xt_native_million_probe.json`: native C probe metadata/summaries; outer Lua wall time was not saved in that historical file.
- The older `rx5600xt_wave32_abc_cba.json` directly in this directory records the earlier ~382k GPU/sec stage. It is distinct from the later copies and must not be overwritten.
- Other specialization/heap/subgroup/hardware files retain their named experiments, including rejected variants and explicit failure handling.

## Retired LÖVE backend history

The backend/app/build code and generated dependencies have been removed after the native/ImGui hardware pass. These historical records remain:

- `latest/love12_build_info.txt`, `latest/love12_wave32_build_info.txt`: original source/runtime-patch provenance; they are not active dependencies.
- `latest/love12_specialized_reference.json`: 5,000 actual detailed fights on LÖVE 12.0.0/Vulkan RX 5600 XT, specialization enabled.
- `latest/love12_specialized_comparison.json`: diagnostic-only 1.0424 DPS MAE and approximate bounds 0–4.1103, with unresolved acceptance blockers.
- `latest/love12_wave32_benchmark.json`: actual 1.331M GPU / 1.049M steady / 0.803M evaluation median. This is historical migration evidence, not the current acceptance target.

Paths and hashes inside those artifacts describe the original run. Deleted source/build paths must not be relabeled as current files.

## Initial software-Vulkan diagnostics

- `headless_fast_baseline.json`: 5,000 completed real shader fights with metadata and aggregate reports.
- `headless_fast_frozen_suite.json`: exact original reference snapshot.
- `headless_fast_comparison.json`: diagnostic-only 1.0424 DPS MAE and sampling bounds against saved Forever means; the reference engine was not rerun.
- `archived_webgl_mean_comparison.json`: saved means, not per-fight source replay.
- `fast_detailed_replay.json`: initial failure exposing corrupt/repeated detailed tail fields.
- `fast_detailed_replay_fixed.json`: corrected 325-fight replay, 24 common fields and different batch sizes.
- `headless_fast_replay_stats.json`, `headless_detailed_replay_stats.json`: 65-fight-per-case replay aggregates; do not compare them directly with the older 1,000-fight-per-case reference means as a mechanics regression.

Recompute the historical diagnostic with current native tools:

```sh
node tools/compare_parity.mjs validation/evidence/headless_fast_baseline.json \
  --suite validation/evidence/headless_fast_frozen_suite.json
```

This exits 1 because diagnostic suites cannot pass acceptance. New exact replay uses native `--raw-output` or frontend `--raw-reports` with the comparison tools. **No evidence here certifies accepted ≤1 DPS MAE against an approved Forever matrix.**
