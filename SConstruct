#!/usr/bin/env python
"""VortarisECS - Godot GDExtension build script.

Prerequisites:
  pip install scons
  A godot-cpp checkout (https://github.com/godotengine/godot-cpp, branch
  matching your Godot version, e.g. 4.7), pre-built with:
      scons platform=windows target=template_debug arch=x86_64

Build:
  scons platform=windows target=template_debug arch=x86_64 \
        godot_cpp_path=<path-to-godot-cpp> build_library=False
"""

import os

# ---------------------------------------------------------------------------
# Locate the godot-cpp checkout. Priority:
#   1. command line:  scons godot_cpp_path=<path>
#   2. env var:       GODOT_CPP_PATH
#   3. common locations: <repo>/godot-cpp, <repo>/../godot-cpp, cwd/godot-cpp
# ---------------------------------------------------------------------------
godot_cpp_path = ARGUMENTS.get("godot_cpp_path", "") or os.environ.get("GODOT_CPP_PATH", "")
if not godot_cpp_path:
    # `__file__` is not exposed by some SCons/Python 3.14 combinations; fall back
    # to the current directory (scons is run from the repo root).
    sconstruct_dir = os.path.dirname(os.path.abspath(globals().get("__file__", os.getcwd())))
    root = sconstruct_dir
    for cand in (
        os.path.join(root, "godot-cpp"),
        os.path.join(root, "..", "godot-cpp"),
        "godot-cpp",
    ):
        if os.path.isdir(cand):
            godot_cpp_path = cand
            break
if not godot_cpp_path:
    raise RuntimeError(
        "godot-cpp not found. Pass godot_cpp_path=<path> on the command line "
        "or set the GODOT_CPP_PATH environment variable."
    )

env = SConscript(os.path.join(godot_cpp_path, "SConstruct"))

env.Append(CPPPATH=["src/", "demo/"])

sources = (
    Glob("src/core/*.cpp")
    + Glob("src/reflect/*.cpp")
    + Glob("src/serialization/*.cpp")
    + Glob("src/network/*.cpp")
    + Glob("src/gdscript/*.cpp")
    + Glob("src/demo/*.cpp")
    + ["src/register_types.cpp"]
)

# GDExtension class reference (doc_classes/*.xml) is compiled into editor /
# template_debug builds so the in-editor help (F1) and class reference show the
# documentation. Skipped for release builds, where it is not needed.
if env["target"] in ["editor", "template_debug"]:
    doc_xml = Glob("doc_classes/*.xml")
    if doc_xml:
        doc_data = env.GodotCPPDocData("src/gen/doc_data.gen.cpp", source=doc_xml)
        sources = sources + [doc_data]

# SCons prefixes shared libraries with 'lib' on Unix. The .gdextension
# references unprefixed file names, so strip the prefix on every platform.
env["SHLIBPREFIX"] = ""

# godot-cpp v10 names the platform 'linux'; Godot's .gdextension key and the
# add-on layout use 'linuxbsd'. Normalize so the artifact matches the package
# (windows/macos are unchanged).
out_platform = "linuxbsd" if env["platform"] == "linux" else env["platform"]
library = env.SharedLibrary(
    "demo/addons/vortarisecs/bin/vortarisecs.{}.{}.{}{}".format(
        out_platform, env["target"], env["arch"], env["SHLIBSUFFIX"]),
    source=sources,
)

env.NoCache(library)
Default(library)
