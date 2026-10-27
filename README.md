# Fastsim

Fastsim is a fast combat simulator for specialization (spec) analysis. It runs combat workloads on a Vulkan GPU and provides tools to compare builds and action policies, inspect counterfactual decisions, and search for stronger policies. The desktop app supports importing WoWSims profiles, editing a build and action priority list (APL), and running analyses.

The current implementation focuses on WoW Classic/Forever Warlock. WoWSims import accepts supported local JSON exports; unsupported versions, gear effects, and settings are reported rather than silently approximated. Simulation and search results are analysis tools, not claims of globally optimal play or verified game balance.

## Build

The native frontend currently targets Linux. On Windows, build and run it in WSL2 with an Ubuntu distribution and a working Vulkan-capable graphics setup; native Windows builds are not supported yet.

### Linux

Install a C++17 compiler, CMake, Ninja, SDL3 3.2+, OpenGL development files, libpng, nlohmann-json 3.10+, Node.js 18+, LuaJIT, a C compiler, Vulkan headers/loader and a Vulkan driver, `glslangValidator`, `spirv-val`, and `sha256sum`. Install these from your distribution packages or the corresponding upstream project packages. Then, from the repository root:

```sh
./tools/build_imgui.sh
./tools/run.sh
```

The build creates `artifacts/imgui-build/`; the app writes run outputs under `artifacts/`. To run without opening a window:

```sh
./tools/run.sh --headless
```

### Windows (WSL2)

Install WSL2 with Ubuntu, clone the repository inside the Linux filesystem, install the Linux dependencies listed above, and configure WSLg plus Vulkan GPU support for your Windows GPU driver. In the Ubuntu terminal:

```sh
cd ~/fastsim
./tools/build_imgui.sh
./tools/run.sh
```

Use `./tools/run.sh --headless` to run without the desktop window. The app and Vulkan worker run inside WSL2; the Windows-native toolchain is not currently supported.

## Workflows

### Import a WoWSims profile and simulate

Open the app, drag a supported local WoWSims Classic/Forever JSON export into the import area, review or edit the build and APL, then select **Simulate**. You can also prepare a candidate JSON with the editor and evaluate it from the command line:

```sh
./tools/run.sh --headless --candidate path/to/candidate.json --runs 1
```

#### Current import limitations

The importer currently accepts **WoWSims/Forever Warlock JSON exports using API version 2**. Other classes and export versions are not supported. Imported builds also have these limits:

- Prepull actions, item swaps and custom cooldown schedules are rejected.
- Gear is converted to recognized direct stats; item-specific effects are not generally simulated. Only known gear/enchant stats and a limited trinket effect are mapped. Unknown item IDs may contribute no stats.
- Armor effects, pets/sacrifices beyond none, Imp or Succubus, and assigned curses beyond Curse of the Elements are unsupported.
- Encounters support one level-63 target and fixed duration; multiple targets, other levels, variable duration and health-based encounters are unsupported.
- Only explicitly mapped buffs, debuffs, consumables and bonus stats are accepted. Active unsupported settings, encodings, consumable IDs or stats can cause import rejection.
- APLs must use supported Warlock actions and conditions. The compiler allows up to 32 rows, at most two `AND` conditions per row, and no `OR`; Drain Soul is not implemented by the current kernel.

Simulation summaries and per-run results are saved in a fresh job directory under `artifacts/` (or use `--output-dir PATH`). The app's **Run detailed** action produces damage breakdowns, uptimes, and a first-fight event timeline.

### Analyze counterfactual regret

In **Analyze Regret**, choose a build and search budget, then select **Analyze Session Regret**. This compares legal action alternatives at selected decisions in fight #1, continuing with the APL after each choice. It is a local counterfactual diagnostic, not a complete policy score. A small command-line example:

```sh
./tools/run.sh --headless --regret --candidate path/to/candidate.json \
  --regret-decisions 2 --regret-rounds 8 --regret-depth 2 \
  --regret-samples 4 --regret-validation 8
```

Regret rows and the analysis summary are saved with the job output. Regrets overlap and should not be summed.

### Benchmark policies

The **Policy Benchmark** tab compares a complete-fight controller with the candidate's original APL, using matched evaluation seeds. Choose either **MCTS + PUCT** or **Neuroevolution MLP**, configure the search/training budget, then select **Run Policy Benchmark**. MCTS replans after each committed action; neuroevolution evolves a small neural policy through mutation and selection. These are separate policy methods: neuroevolution does not use MCTS.

Small CLI examples:

```sh
# MCTS + PUCT controller
./tools/run.sh --headless --policy-benchmark --policy-fights 8 --policy-puct 2

# Neuroevolution MLP
./tools/run.sh --headless --policy-benchmark --policy-algorithm neuroevolution \
  --policy-fights 32 --evolution-generations 64 --evolution-population 16 \
  --evolution-seed 42

# PPO MLP
./tools/run.sh --headless --policy-benchmark --policy-algorithm ppo \
  --policy-fights 32 --ppo-iterations 64 --ppo-rollouts 32 --ppo-lr 0.003 \
  --evolution-seed 42
```

Use more matched fights and a fresh seed range to evaluate tuned policies. Treat these as experiments: search budgets and seeds affect results, and a benchmark does not establish a globally optimal policy.

## More information

- [Native frontend details](native/README.md) — controls, options, outputs, and analysis behavior.
- [STATUS.md](STATUS.md) — current implementation status.
- [CHECKLIST.md](CHECKLIST.md) and [TODO.md](TODO.md) — validation gates and planned work.
