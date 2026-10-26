# Thin native frontend — first slice

`ui/main.cpp` is an experimental Dear ImGui job client. `frontend/native_job.*` provides a renderer-independent submission/status/cancellation interface. Both GUI and `--headless` call that interface. It launches the existing `tools/run_native.lua` / `tools/vulkan_probe.c` compute path unchanged, with immutable arguments and unique result/work directories.

This is **process isolation, not the planned persistent in-process C engine library**. The existing engine exits on Vulkan failures; retaining process isolation gives the first UI safe failure/cancellation behavior without refactoring its GPU resource ownership prematurely. Repeated jobs still have native process/setup costs. Library extraction, persistent device/pipeline/buffer reuse and wider request support remain TODOs.

## Ownership and performance

- The worker's native C code owns Vulkan, queue submission, shaders/SPIR-V, subgroup enforcement, buffers, batches, cached host-visible readback, report validation and C Welford aggregation. `tools/vulkan_probe.c` is not duplicated or rewritten by this frontend.
- The frontend has no compute buffers, dispatch calls, per-fight report decoder, per-GCD callbacks or per-frame simulation scheduling. It reads bounded batch log messages and validates/loads completed candidate aggregates.
- Dear ImGui uses SDL3/OpenGL3 for window rendering only. Simulation remains Vulkan. A separate renderer avoids coupling UI graphics resources to the compute device; the eventual renderer can be replaced without changing the job interface.
- Compute-priority mode finishes its announcement frame with `glFinish()` before launching the worker, then stops issuing UI rendering/swap calls until the native job ends. It still pumps input, updates the title with coarse progress, and handles Escape/close. This does not assert that the desktop compositor/other applications consume no GPU resources.
- Optional live mode renders at approximately 10 Hz. Its results are labeled `visible-10hz`; paused and headless results have distinct labels. UI frame counts and dependency revision are saved with the worker summary.
- Cancel/close/SIGINT/SIGTERM requests terminate the isolated process group, escalate if needed, drain/reap the worker, remove its owned temporary/partial outputs, and reject accepted throughput. Already completed run artifacts remain historical, but the cancelled job has no successful median summary.

## Build and run (Linux)

```sh
./tools/build_imgui.sh
./tools/run.sh

# GUI-attached native benchmark, then exit. Default: rendering-paused.
./tools/run.sh --benchmark

# Same native worker, no SDL/window initialization:
./tools/run_imgui.sh --headless

# Live rendering is a separate comparison mode:
./tools/run_imgui.sh --benchmark --live-ui
```

Dependencies: C++17 compiler, CMake/Ninja, Threads, SDL3 >=3.2, OpenGL, libpng, nlohmann-json >=3.10, Node.js >=18 (editor preparation), plus existing native runner tools (LuaJIT, C compiler, Vulkan, glslangValidator, spirv-val, sha256sum). The current machine already supplies these; no network fetch is used by the frontend build. Dear ImGui version/commit, unmodified source hashes and MIT license are pinned under `third_party/imgui/`. Set `FASTSIM_BUILD_JOBS` to change the default four compiler jobs. Build products are ignored under `artifacts/imgui-build/`.

Defaults: three runs of one million fights, shared-feature specialization, required wave32, heap40, 65,536-fight batches and two discarded warmup batches. The fixed suite contains five candidates; total fights must be divisible by five, at least ten. `--generic` selects the existing native generic/default-width control; `--no-subgroup-size` retains specialization without forcing width. `--runs`, `--total-fights`, `--batch-size`, `--warmup-batches`, `--output-dir` and bounded/explicit `--raw-reports` support diagnostics. The frozen benchmark retains these constraints; the editor also submits one immutable edited candidate.

Each submission gets a fresh directory under `artifacts/imgui/`, or the explicitly supplied **new** `--output-dir`. `native-N.json` preserves each original native result. `summary.json` records timing scope, presentation mode, accepted repeats, medians, UI observations and errors. Engine build key, runner hash, suite/kernel/input identity and variant come from native output. GPU, C aggregation, native full-run, native steady and client-process times remain separate; GUI rates are not inferred from native historical numbers.

## Correctness and performance gates

```sh
# Default: raw million-fight replay, then five alternating aggregate-only pairs.
node tools/benchmark_imgui.mjs

# Separately measure a live-rendering frontend:
node tools/benchmark_imgui.mjs --live-ui
```

The paired harness compares full report-word output across headless/GUI modes before timing acceptance, then checks matching source/input/device/results for all aggregate-only measurements. Raw replay timing is excluded from the performance comparison and its binaries are removed after the gate; replay counts/results are preserved. A passing hardware result requires at least five pairs, median GUI GPU time ≤1.02× headless and steady evaluation ≤1.05× headless. Any correctness failure rejects speed summaries. A measured regression exits nonzero. Paused mode must record zero UI render frames during native jobs.

The RX 5600 XT hardware experiment passed: five alternating pairs, **1,565,063 headless / 1,566,647 paused-GUI GPU fights/sec**, exact replay of one million fights / 16 million report words, and zero paused-mode UI render frames. GPU-time ratio 0.998989 and steady-time ratio 0.995598 meet the gates. [Archived full experiment](../validation/evidence/latest/rx5600xt_imgui_paired/summary.json). Small differences are not a speedup claim. Live rendering, diverse workloads and other devices need independent measurement. Keep the standalone CLI baseline too (`./tools/benchmark_gpu.sh`); relative overhead and absolute throughput remain separate checks.

Small software diagnostics (not speed evidence):

```sh
./tools/run_imgui.sh --headless --allow-software --no-subgroup-size \
  --total-fights 10 --runs 1 --batch-size 3 --warmup-batches 0

# SDL offscreen OpenGL smoke if supported:
SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 ./tools/run_imgui.sh --ui-frames 3
```

Tests: `./tools/build_imgui.sh` builds and runs CTest worker lifecycle/identity/rejection/cancellation tests with synthetic process results. `./tools/check.sh` retains native/shader/APL/reference tests. Neither synthetic rates nor software-Vulkan rates establish the hardware target. Persistent native library extraction, live-rendering hardware coverage and arbitrary multi-candidate batches remain open in TODO.md. LÖVE runtime/app/build dependencies and backend-only tests are removed; the default launcher now selects ImGui.


## Build editor

The normal window evaluates one edited candidate. APL text, talent ranks, race,
pet/sacrifice and direct stats/buffs are resolved by `tools/editor_config.mjs`,
shared with `--candidate FILE` in GUI/headless mode. The worker receives an
immutable `suite.json`; it checks layout/kernel identity and uses the same
specializer, shader cache, Vulkan transport and C aggregation. Candidate source
is archived alongside results. Preparation occurs before native job timing.

The frozen benchmark remains available in Advanced and is still the default
for `--benchmark`/`--headless` without `--candidate`. Icons load once before jobs;
rendering stays paused by default. Detailed output is enabled only by **Run detailed** or `--detailed`; normal Simulate uses fast mode.

Imports target local Forever/Classic apiVersion 2 JSON exports with direct bonus
stats. Equipped gear, other versions, compressed links and unsupported effects
are rejected. The editable reference presets are separate from the immutable
historical benchmark. Detailed analysis is available through Run detailed.

`node validation/editor_config.mjs` covers round trips, packing identity, APL
line errors, talent requirements and rejected imports. `lua validation/json_input.lua`
covers safe data-only snapshot parsing. `tools/benchmark_imgui.mjs --candidate FILE`
checks exact replay and presentation overhead for an edited input. Hardware
throughput still requires the paired hardware gate; software smoke runs do not
establish it.


## DPS histogram

All fights in the last repeat are shown automatically. The C aggregation pass
updates 256 counters per candidate using DPS it already computes. The range
starts at 0–1024 DPS and doubles when needed, merging adjacent counters without
losing samples. UI plotting trims empty bins and merges only adjacent intervals.
No raw reports, extra dispatches, transfers or second simulations are required.
Counters are validated against sample counts before publishing results.

The GUI/CLI launcher enables collection by default. Advanced > DPS histogram
or `--no-histogram` disables it. The low-level native runner has the same flag;
`FASTSIM_DPS_HISTOGRAM=0` disables it when invoking the C probe directly.

`fastsim-histogram-test` checks range expansion, display rebinning, complete
counts and rejection of corrupt histograms. Its `--integration "$PWD"` mode
checks actual Vulkan reports are bit-identical with collection on/off and bins
match the raw DPS. `node tools/benchmark_histogram.mjs` measures the production
aggregation loop over one million synthetic valid reports, using alternating
pairs, warmup, and a fixed CPU. Add `--vulkan --total-fights 1000000` for exact
million-fight report replay and paired hardware timing. `--allow-software` is
available for diagnostics; software rates do not establish hardware performance.

Nine alternating RX 5600 XT pairs at one million fights passed exact report replay
(16 million words) and complete histogram-count verification. Median native wall
was 982.013 ms disabled / 981.142 ms enabled; GPU time was 637.650 / 636.640 ms.
The small favorable differences are noise, not a speedup. A separately pinned
CPU benchmark of the production aggregation loop measured 7.420 / 7.543 ms,
about 0.123 ms added per million synthetic reports. See the full JSON evidence in
`validation/evidence/latest/histogram_paired/summary.json`.


## Detailed analysis and event trace

`Run detailed` uses the configured fights/repeats and produces `details-N.json`.
Damage breakdowns include each player spell and separate pet melee, spell and
Demonic Brand damage. Statistics are per-fight averages; uptime fractions and
active seconds are averaged across all detailed fights. Mana counters and damage
reconciliation are retained in the JSON output.

After detailed aggregation, the worker replays fight index 0 for each candidate
with the same seed and packed inputs. `tools/detailed_trace.lua` derives an
instrumented shader from the pinned detailed source; no generated shader file
is changed and the fast path never loads the trace modules. It captures queued
events, cast starts/casts, direct/periodic damage, misses, Life Tap and pet
attacks, with timestamps, player/pet mana and outcome flags. The first 163 words
must exactly match a separate uninstrumented replay before trace data is saved.

Each build has a bounded 8,192-record buffer. Total and stored record counts
are saved separately, and the UI warns when a long-fight timeline is truncated.
The timeline represents one fight, not averaged events. The UI caches completed
analysis, provides three actor lanes and a clipped event table, and supports
search, damage-only filtering and time zoom. Native summary snapshots retain
only the detailed artifact path, not thousands of event objects.

`timing.detailsMs` separates trace/postprocessing cost from ordinary dispatch
and C aggregation timings. Detailed native wall time includes diagnostics;
never compare it directly to the fast benchmark rate. `--raw-reports` still
exports only the requested aggregate-mode records (16 or 163 words per fight).

Checks: `luajit validation/detailed.lua`, trace shader/SPIR-V validation through
`tools/check.sh`, synthetic detailed-worker routing/rejection tests, and
`node validation/detailed.mjs` for a small actual Vulkan end-to-end GUI-worker
submission that checks reports, counters and unchanged fast mode.

## Regret analysis (opt-in)

**Analyze Session Regret** analyzes fight index 0 of the current immutable build
and seed. The frozen-suite selector can analyze any of its five builds. Search
budgets control decision coverage, UCT rounds, plan depth (1–4), search samples,
and fresh validation samples. Roots are spread over eligible decisions, capped
at 128; capture retains up to 1,024 decisions and reports truncation.

The default is a **batched one-action comparison with APL continuation to the
fight end**: analyze up to 128 eligible decisions, evaluate every supported legal
first action with 1,000 paired future samples, then run the current APL to terminal.
Different root/action/sample combinations share dispatches of up to 65,536 fights.
The initial capture uses a trace buffer; rollouts return only 32 words (128 bytes),
including completion time, root context, and total Doom damage. Each returned fight
must be complete at the configured encounter end. A zero regret means the taken
action matched the best alternative; it does not mean the spell dealt zero damage.
The inspector separately shows average Doom damage when evaluating Doom.

Plan depth greater than 1 enables CPU **open-loop MCTS / UCT with GPU batched
rollouts**. Nodes are action sequences, not observation-conditioned policies.
These deeper comparisons can use different searched follow-up actions for each
first action. The UI identifies the continuation semantics. Search expands
prefixes legal in every sampled state, rejects invalid/unreachable prefixes, and
evaluates the best found plan per root action on fresh paired future samples.
If a longer plan fails held-out legality it falls back to that first action plus
APL. Search sample count and UCT rounds apply only to this optional deeper mode.

Rollouts replay the exact original fight prefix, sample a new future RNG stream
at the root, execute the forced action or searched prefix, and then return to the
existing APL. Candidate actions come from enabled APL actions, its filler and Life
Tap, with duplicate spell aliases removed. Automatic cooldowns run normally; a
forced action replaces the APL evaluation at that decision, including explicit
off-GCD APL rows. Search does not optimize those rows, waiting, or pet controls.
Every job checks all 16 report words against the original fast shader, including
unchanged-action replays at selected roots. Capture retains 1,024 decisions and
reports truncation; budgets below eligible decision count spread roots over the
fight. Paired approximate 95% intervals do not correct for winner selection.
Regrets overlap and must not be summed. Future random draws can diverge between
actions even with paired seeds. No one-second latency guarantee is established.

All compute is isolated in `tools/run_regret.lua`, a derived diagnostic shader
from `tools/regret.lua`, and a persistent diagnostic adapter generated by
`tools/regret_transport.lua`. The adapter uses a capture pipeline and a compact rollout pipeline, each reused
for its diagnostic phase. The pinned fast/detailed shaders, ordinary native
runner, specialization and original Vulkan transport are unchanged. No analysis
runs during Simulate or the benchmark. Diagnostic outputs have no accepted
throughput medians. This isolation is a design constraint; performance preservation
still needs the user's separate benchmark run.

Artifacts retain the input snapshot, `native-1.json` (analysis) and `summary.json`.
Cancel/Escape/close terminates the entire worker process group and removes partial
output/work, using the existing lifecycle. Completed analysis remains visible in
the UI when another simulation is run, labeled by its saved candidate/seed.

A small correctness-only run (no performance benchmark):

```sh
./tools/run.sh --headless --regret --candidate candidate.json \
  --regret-decisions 2 --regret-rounds 8 --regret-depth 2 \
  --regret-samples 4 --regret-validation 8
luajit validation/regret.lua
node validation/regret.mjs
```

Saved results can be inspected without recomputing:

```sh
./tools/run.sh --regret-result artifacts/imgui/JOB/summary.json
```

The JavaScript integration check uses small correctness workloads, a 1,000-sample
batched case, five-build replay, delayed Doom ticks, artifact routing, and
cancellation. It makes no throughput claim.
