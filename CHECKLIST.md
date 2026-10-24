# Delivery checklist

Current measurements are in [STATUS.md](STATUS.md); remaining implementation details are in [TODO.md](TODO.md).

## 1. Native/ImGui cutover

- [x] Build the thin Dear ImGui frontend with pinned sources/license/hashes.
- [x] Keep compute in the existing isolated native Vulkan/C worker, independent of UI rendering/frame pacing.
- [x] Pass hardware million-fight replay: every one of 16 million words matches headless.
- [x] Pass five alternating hardware timing pairs: GPU 1.565M headless / 1.567M GUI, within ≤2% GPU-time / ≤5% steady-time overhead gates.
- [x] Record zero frontend render frames during paused-priority jobs; keep live mode separately labeled.
- [x] Archive the complete hardware experiment, original native results and source/build/presentation identities.
- [x] Make `tools/run.sh` launch ImGui; remove LÖVE app/config, runners, patches/build scripts, generated build/dependencies and backend-specific tests.
- [x] Preserve historical evidence and frozen suite/shader/layout hashes; verify exact wrapped Vulkan compiler inputs are unchanged.
- [x] Retain native JSON/APL/specialization/parity/replay/shader/C checks and C++ worker lifecycle/cancellation tests.

## 2. Persistent native engine

- [ ] Extract a reusable C engine ABI and explicit error/resource lifetime handling; keep CLI and GUI on one implementation.
- [ ] Reuse devices, pipelines and buffers across jobs; avoid shader generation on cache hits.
- [ ] Preserve wave32 enforcement, heap40, 65,536-fight batches, cached readback and C validation/aggregation.
- [ ] Repeat full raw replay and paired hardware performance gates after compute-path changes.
- [ ] Keep cold startup/compilation, warmups, steady evaluation, GPU intervals and client overhead separately visible.

## 3. Coverage and accuracy

- [ ] Extend detailed/edge/mixed-input, native cancellation/retry/device-failure and cleanup coverage on hardware.
- [ ] Measure continuous live-rendering overhead separately from the accepted paused mode.
- [ ] Add arbitrary packed candidates and a representative 1,000 distinct APL/talent × 1,000-fight benchmark; diverse throughput remains unmeasured.
- [ ] Add generic wave32 as an independent native control.
- [ ] Pin/audit Forever engine/data/inputs/APLs/schema adaptations, freeze the approved matrix and collect adequate independent samples.
- [ ] Establish accepted ≤1 DPS MAE with uncertainty and no unresolved blockers. Current 1.0424 DPS diagnostic MAE is not acceptance.

## 4. Later product/search work

- [ ] Add validated configuration/APL persistence and resolution, job objectives and uncertainty exports.
- [ ] Implement reproducible APL evolution and independent finalist reevaluation.
- [ ] Define neural/MCTS policy contracts before implementation/performance claims.
- [ ] Add another class with its own accuracy suite, and package supported desktop targets.
