#!/usr/bin/env python
"""VortarisECS - Godot GDExtension build script.

Prerequisites:
  pip install scons
  scons platform=windows target=template_debug arch=x86_64   # in the godot-cpp dir, to build the static lib

Build:
  scons platform=windows target=template_debug arch=x86_64 build_library=False
"""

import os

# Absolute path to the godot-cpp checkout this plugin is built against.
GODOT_CPP_PATH = r"C:/Users/Administrator/Desktop/godot-cpp-master"

env = SConscript(os.path.join(GODOT_CPP_PATH, "SConstruct"))

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

library = env.SharedLibrary(
    "demo/bin/vortarisecs{}{}".format(env["suffix"], env["SHLIBSUFFIX"]),
    source=sources,
)

env.NoCache(library)
Default(library)
