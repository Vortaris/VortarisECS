# Cross-platform build

VortarisECS is a GDExtension built with godot-cpp. The **core code is fully
portable** — it uses only standard C++ and godot-cpp's cross-platform API (no
platform conditionals, no MSVC/GCC-specific syntax), and the binary serializer
uses explicit little-endian encoding so snapshots are byte-identical across
platforms. Every release ships a prebuilt **Windows x86_64** plugin; other
platforms are built once on their own machine and dropped into `demo/bin/`.

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

Artifacts land in `demo/bin/` as `vortarisecs.<platform>.template_<target>.<arch>.{so,dylib}`.
`demo/vortarisecs.gdextension` already lists the `linuxbsd` and `macos` entries,
so once the files are in place the editor picks them up on that platform. The
first time you open the project, Godot generates `.godot/extension_list.cfg`.

## Verification

```bash
godot --headless --path demo                                   # functional demo
godot --headless --path demo --script res://scripts/regression_test.gd   # T1-T14, exit 0
godot --headless --path demo --script res://scripts/quickstart.gd
godot --headless --path demo --script res://scripts/perf_test.gd
```

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
