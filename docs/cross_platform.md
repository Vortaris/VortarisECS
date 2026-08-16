# Cross-platform build

VortarisECS is a GDExtension built with godot-cpp. The **core code is fully
portable** — it uses only standard C++ and godot-cpp's cross-platform API (no
platform conditionals, no MSVC/GCC-specific syntax), and the binary serializer
uses explicit little-endian encoding so snapshots are byte-identical across
platforms. Every release ships a prebuilt **Windows x86_64** plugin; other
platforms are built once on their own machine and dropped into
`demo/addons/vortarisecs/bin/`.

| Platform | godot-cpp platform name | Toolchain | Output |
|---|---|---|---|
| Windows x86_64 | `windows` | MSVC (via SCons) | `.dll` (prebuilt in releases) |
| Linux x86_64 | `linuxbsd` | g++ / clang + SCons | `.so` |
| macOS (universal) | `macos` | clang + SCons | `.dylib` |
| Android / iOS / Web | `android` / `ios` / `web` | platform toolchains | `.so` / `.dylib` / `.wasm` |

## Prerequisites

- **Godot 4.7** editor binary for the target platform.
- **godot-cpp** checkout matching Godot 4.7, e.g.
  `git clone -b 4.7 https://github.com/godotengine/godot-cpp.git`.
- **SCons** (`pip install scons`).
- A C++ compiler for the platform (MSVC on Windows, g++/clang elsewhere).

Build the godot-cpp static library for the target platform first, e.g.:

```bash
cd godot-cpp
scons platform=linuxbsd target=template_debug arch=x86_64
scons platform=linuxbsd target=template_release arch=x86_64
```

## Building the plugin

From this repository root, point the build at your godot-cpp checkout and
select the platform:

```bash
# Linux x86_64
scons platform=linuxbsd arch=x86_64 target=template_debug build_library=False godot_cpp_path=<path-to-godot-cpp>
scons platform=linuxbsd arch=x86_64 target=template_release build_library=False godot_cpp_path=<path-to-godot-cpp>

# macOS universal
scons platform=macos arch=universal target=template_debug build_library=False godot_cpp_path=<path-to-godot-cpp>
scons platform=macos arch=universal target=template_release build_library=False godot_cpp_path=<path-to-godot-cpp>
```

Artifacts land in `demo/addons/vortarisecs/bin/` as
`vortarisecs.<platform>.template_<target>.<arch>.{so,dylib}`.
`demo/addons/vortarisecs/vortarisecs.gdextension` already lists the `linuxbsd`
and `macos` entries, so once the files are in place the editor picks them up on
that platform. The first time you open the project, Godot generates
`.godot/extension_list.cfg`.

## Verification

The same headless checks work on every platform (they depend only on the demo
project and the plugin DLL/so/dylib you built):

```bash
godot --headless --path demo                                   # functional demo (expect "=== VortarisECS Demo OK ===")
godot --headless --path demo --script res://scripts/regression_test.gd   # T1-T42, 286 assertions; exit 0 = all pass
godot --headless --path demo --script res://scripts/quickstart.gd        # minimal convenience-API example
godot --headless --path demo --script res://scripts/perf_test.gd         # performance baseline
```

The regression suite (`extends SceneTree`, runs in `_initialize()` and calls
`quit(0/1)`) is the primary gate: any structural/behavioral change must keep it
green. New behavior is added as a numbered `_test_tN_*` in
`demo/scripts/regression_test.gd` and called from `_initialize()`.

### 0.2.x debug tooling (works headless on any platform)

Since 0.2.0/0.2.1 the demo also ships headless/AI-friendly debugging entry
points — a one-shot stats dump, a JSON snapshot export, and a runtime overlay.
They work identically on every platform (they run after the world is built):

```bash
godot --headless --path demo -- --vortaris-ecs-stats          # print get_debug_stats() JSON, exit 0
godot --headless --path demo -- --vortaris-ecs-snapshot save.json   # export JSON snapshot to user://, exit 0
godot --path demo -- --vortaris-ecs-overlay on                # launch with the runtime overlay (F2 toggles)
```

Output is `[vortarisecs]`-prefixed. Full parameter table and MCP `run_script`
examples are in [`docs/AI_DEBUGGING.md`](AI_DEBUGGING.md).

## Notes

- **Endianness**: serialization is explicitly little-endian; all mainstream
  platforms are little-endian, so snapshots round-trip identically.
- **Precision**: script-defined (schema-only) component layouts derive from
  `sizeof`/`alignof` of the real Godot types, so they stay correct regardless
  of `precision=double` builds.
- **Docs (F1)**: the class reference is compiled into editor / `template_debug`
  builds only; release builds omit it (matches Godot conventions).
- The prebuilt release assets contain the **Windows x86_64** plugin. For other
  platforms, build as above — no source changes are required.
