# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & verify (Windows / MSVC / SCons)

The plugin links godot-cpp (external dependency, must target Godot 4.7). Build
its static library first: `scons platform=windows target=template_debug arch=x86_64`.
Point the plugin build at your checkout with `godot_cpp_path=` (or the
`GODOT_CPP_PATH` env var). `godot` below is your Godot 4.7 binary.

```bash
# Build the plugin DLL (outputs to demo/bin/)
scons -j 8 platform=windows target=template_debug arch=x86_64 build_library=False godot_cpp_path=<path-to-godot-cpp>

# Functional demo (expect "=== VortarisECS Demo OK ===", exit 0)
godot --headless --path demo

# Regression suite (T1–T32, 129 assertions; exit 0 = all pass)
godot --headless --path demo --script res://scripts/regression_test.gd

# Minimal convenience-API example + perf baseline
godot --headless --path demo --script res://scripts/quickstart.gd
godot --headless --path demo --script res://scripts/perf_test.gd
```

Rules of thumb when changing code: **every structural/behavioral change must keep
the demo AND the regression suite green**; run them together. First run of a
fresh demo checkout needs `.godot/extension_list.cfg` (open the project in the
editor once).

## Architecture

Two layers, by design — the pure C++ core never touches Variant on hot paths:

- **`src/core/` (`namespace vortaris`)** — the ECS engine: `World`, `Archetype`
  (SoA column storage, swap-remove), `Column` (aligned row buffers + lazy change
  tracking), `Query`/`QueryCache` (incrementally maintained archetype→query
  index), `CommandBuffer` (deferred structural changes), `ObserverDispatch`
  (COW callback list), `ComponentRegistry` (global, maps name↔type id↔schema).
- **`src/gdscript/` (`VECS`-prefixed classes)** — thin GDExtension binding on
  top of `vortaris::World`. `VECSWorld` is a Node registered as the `VECS`
  engine singleton. `src/reflect/`, `src/serialization/`, `src/network/`
  provide schema reflection, deterministic binary + JSON snapshots, and the
  pluggable sync strategy.

### Key invariants (violating these is the classic source of bugs here)

- **Iteration contract**: structural changes (add/remove component, destroy
  entity) are FORBIDDEN inside `for_each`, `View::each`, or `VECSWorld::each` —
  archetype rows move under the walker. The core rejects them loudly via
  `iteration_depth_`; defer them to `commands()`. Reading/writing component
  *values* in a loop is fine.
- **Deferred ops net, not apply**: `_commit_deferred_move` computes the final
  component set from the op sequence (`[add A, remove A]` nets to no-A). Events
  are dispatched per net change, not per op.
- **Swap-remove everywhere**: `remove_entity` swaps the last row into the hole.
  Row indices are stable only until the next structural change. `Column`
  versions must follow the entity (they do — `swap_remove` copies them with the
  data).
- **Change tracking is lazy**: a column tracks versions only after
  `ensure_versions(tick)` is first called; it stamps pre-existing rows with the
  current tick so the first `changed()` pass reports them once. `mark_changed`
  bumps the global `change_tick_`, which `changed()` baselines compare against
  (`version > baseline`).
- **`clear()` (snapshot load) does not dispatch Removed** — loading a save
  replaces the world; it is not "death". Network full-state application is
  idempotent (rebuilds an existing id).
- **Schema-only components** (registered from GDScript) get their layout from
  `sizeof`/`alignof` of the real Godot types in `component_registry.cpp` — do
  not hardcode sizes (Transform3D is 48 bytes, Basis 36 + origin 12).

### Data access styles (choose deliberately)

- `World::for_each<T...>` / `view<T...>()` — typed C++ iteration, zero Variant.
  `for_each` caches its compile-time id list; both are guarded by the iteration
  contract.
- `ChangeView<T...>` / `query().changed([...])` — only entities whose watched
  columns were written since the baseline. Baseline keys mix the membership
  signature with the changed set, so filters don't interfere.
- `VECSWorld::each(comps, callable)` — GDScript iteration without materializing
  an Array; `spawn()` / `getf`/`setf` are the convenience sugar.

### Networking

`VECSNetworkSync` (Node) binds one world and owns a `VECSSyncStrategy`.
`VECSSnapshotReplication` is the default server-authoritative strategy: server
tracks dirty networked components, sends spawns/deltas/despawns, and a periodic
full-state reconciliation. Binary deltas are read with a strict cursor —
**readers must consume every byte of a skipped entity/component** or the rest of
the packet deserializes garbage (the `apply_delta` dead-entity path consumes
into a scratch buffer).

## Testing

No C++ unit tests (core depends on godot-cpp runtime). Regression coverage is
GDScript headless scripts in `demo/scripts/`:
`ecs_test_util.gd` (assert helper) + `regression_test.gd` (`extends SceneTree`,
runs T1–T32, `quit(0/1)`). Add a numbered `_test_tN_*` for any new behavior and
call it from `_initialize()`. The functional `main.gd` demo doubles as a
smoke test for the full feature set end-to-end.
