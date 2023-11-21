Blender add-on
==============

This add-on makes anari backends available as renderers in blender.

It is work in progress should be treated as experimental.

## Installing
The plugin will install itself if the blender install directory is
set as `BLENDER_DIR` in the sdk cmake configuration (only tested on linux).
The add-on can then be enabled in the blender preferences.

Alternatively, after building the SDK including Python bindings, the add-on
(`CustomAnariRender.py`) and the Python bindings can be manually copied to a
location where Blender Python sees them. Additionally the cffi module needs
to be installed to the Python install Blender uses.
See the `CMakeLists.txt` in this directory for inspiration.

When launching Blender the anari install directory needs to be in the library
search path for it to find the actual anari library and devices.

## Limitations
* Supports only objects that can be converted to meshes
* Only basic materials (no complex graphs) using either the Principled, Diffuse or Specular shaders
* Only supports directional (Sun) and Point lights
* The renderings are subject to limitations of the anari backends
