# Remaining work

**Native Vulkan/C compute + thin Dear ImGui frontend is the active application.** The frozen workload passed million-fight raw replay and five hardware presentation pairs: **1,565,063 headless / 1,566,647 GUI GPU fights/sec**, paused rendering, RX 5600 XT. [Evidence](validation/evidence/latest/rx5600xt_imgui_paired/summary.json). The LÖVE runtime/app/build dependencies and backend-specific tests are removed; only historical evidence and frozen-source compatibility remain.

## 1. Extract a persistent native engine

- [ ] Refactor `tools/vulkan_probe.c` into a reusable C library with an explicit ABI, errors and resource ownership. The current UI safely isolates its exit-on-error behavior in a worker process; it is not an in-process engine yet.
- [ ] Keep CLI and GUI on the same compute implementation. Preserve shaders/SPIR-V, required wave32, heap40, batch size 65,536, candidate IDs, seeds, completion/failure checks, cached host-visible readback and C Welford aggregation.
- [ ] Reuse device/pipeline/buffer lifetimes across jobs. Derive cheap source/feature signatures before rebuilding specialized source; keep compilation/setup outside steady evaluation.
- [ ] Define versioned packed job/result interfaces for arbitrary candidates/APLs and optional diagnostics. Lua/Node generation/import/validation tools remain useful; no Python dependency.
- [ ] Retain separate initialization/compilation, warmup, GPU dispatch, transfer, C aggregation, steady evaluation and process/client timing.

## 2. Preserve a thin frontend

- [x] Minimal Dear ImGui job client with settings, submit/cancel, coarse progress, aggregate results, timings and failure logs; pinned source/license/hashes.
- [x] Renderer-independent immutable process jobs; the original native engine owns all GPU work and per-fight validation/reduction.
- [x] SDL3/OpenGL3 renders UI only; Vulkan compute stays independent. Default compute priority drains the announcement frame and pauses rendering while jobs execute; input/cancellation stay active.
- [x] Optional approximately 10 Hz live mode has a separate label; saved observations include frontend frames and source/build/dependency identity.
- [ ] Keep per-event/GCD callbacks, per-fight UI objects/decoding, resource reuploads and frame/vsync pacing out of all future compute integrations.
- [ ] Extend worker recovery/cancellation/failure tests as the persistent API replaces process isolation. Preserve rejection of partial/failed results and unique evidence directories.
- [x] Initial build editor: text talent ranks, APL text + spell icons, direct-stat Forever JSON import, saved drafts and immutable single-candidate submission shared with CLI.
- [ ] Extend import adapters to the user's WoWSims export version and equipped gear; keep unsupported settings explicit.
- [x] Opt-in detailed runs: damage breakdowns, average uptimes and a bounded first-fight event timeline with exact replay checks; preserve the default fast-report path.
- [ ] Repeat paired hardware performance gates after the editor integration (software replay is diagnostic only).

## 3. Maintain performance gates

Target no measurable regression from attaching UI. The historical ~1.56M GPU rate covers the frozen five-build million-fight workload; current native wall timing is around 1M/sec, not the GPU rate.

- [x] Implement raw-replay-first benchmark harness and ≥5 alternating headless/GUI hardware timing pairs; paused-mode hardware result passes.
- [ ] Rerun the standalone native baseline and exact word replay after compute changes; rerun matched pairs with identical workloads, source/input/device identity, cache/warmup state and timing definitions.
- [ ] Keep GUI median GPU time ≤1.02× matched headless and steady evaluation ≤1.05× headless. Investigate repeatable losses even below these ceilings; do not adopt the retired backend's slower rate as the baseline.
- [ ] Keep headless, visible throttled UI and rendering-paused priority results distinct. Shared-GPU rendering is not assumed free; measure live mode separately.
- [ ] Keep failed/incomplete/overflow/nonfinite fights out of accepted throughput; preserve raw diagnostic replay separately from aggregate-only timing.
- [ ] Preserve hardware/source/driver/toolchain provenance and cold versus warm timing; document spread, not just best runs.

## 4. Accuracy and robustness

- [ ] Audit/pin Forever revision/data/mechanics and resolved stats, talents, race, pet, buffs, equipment, APL, encounter and durations. Resolve historical input/APL/schema adaptations.
- [ ] Freeze an approved matrix of specs/pets/racials/resource edges/policies/durations. Compare counters/resources/order/RNG continuation/traces before collecting adequate independent reference samples.
- [ ] Establish accepted **≤1 DPS MAE** with stated precision. Current five-case **1.0424 DPS MAE** and bounds 0–4.1103 remain diagnostic-only.
- [ ] Extend fast/detailed statistics, zero damage, NaNs, overflow, event exhaustion, worker/device failure, retry and cleanup coverage.
- [ ] Keep historical evidence immutable. New mechanics snapshots require new source/input identities and matching regenerated suites.

## 5. Diverse candidates and later scope

- [x] Add immutable single edited-candidate submission to the native interface/frontend alongside the frozen suite. Arbitrary multi-candidate batches remain open.
- [ ] Build **1,000 distinct APL/talent candidates × 1,000 fights** with documented diversity, exact replay and full timings. Shared specialization leaves differing settings as runtime data; stats/APLs always remain runtime inputs. Their speed is unmeasured.
- [ ] Add generic wave32 independently of specialization; the native CLI currently ties explicit subgroup overrides to specialization.
- [ ] Add reproducible APL evolution with held-out seeds and independent finalist reevaluation.
- [ ] Define observations/normalization/action masks/weights/objectives/checkpoints before neural inference, and prefix/snapshot/rollout semantics before MCTS. No forecast is a measurement.
- [ ] Add validated persistence/config resolution, additional classes with independent accuracy suites, and desktop packaging after current engine/job goals.
