# Configuration preparation snapshot

These are preparation-only modules extracted from the user's existing
`yazpads_warlock_sim/threejs_webgl_des/src` tree. `provenance.json` records original
and extracted hashes and local adaptations. The complete `model.js` is byte
identical to the model pinned in the engine's generated manifest. The editor
refuses to pack if that model or its config layout differs from the engine.

`tools/editor_config.mjs` is the shared GUI/CLI entry point: talent validation,
reference JSON import, APL parsing, buff/talent resolution and packed input
creation happen before a job is submitted. Browser UI/search orchestration is
excluded. The existing Lua compiler remains a diagnostic tool for its smaller
APL grammar; the new GUI and `--candidate` CLI both use this preparation service.

When updating mechanics, deliberately regenerate the shaders/layout and update
this snapshot from the same source revision; run the editor, shader and exact
report-replay checks. Do not independently edit the model's packing math.

The editable presets are imports of the reference requests. They are not the
historical packed benchmark inputs: those remain frozen under `data/validation`
and selectable through Advanced > Frozen five-build benchmark.
