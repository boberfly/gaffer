 1.5.0.0.fb.0.0.1 (relative to GafferHQ's 1.5.0.0a2)
=================

Features
--------

- Arnold : Added Arnold Operators support.

Improvements
------------

- Gaffer.cmd (Windows) : Small tweaks to allow externally referenced libraries in a Rez environment
- SConstruct :
  - Allow specifying a custom MSVC version
  - Allow specifying a custom python binary that is outside the Gaffer bin directory
  - Small environment variable fixups to allow external dependencies and namespacing in a Rez environment
  - Make sure to add more includes that are external to Gaffer root
  - Ensure HIPRT, OpenImageDenoise and OpenUSD are linked for Cycles
  - Set ZSTD library to `zstd_static` for windows builds
- Cycles :
  - OpenVDBs will now render and allow shader updates in a live render.
  - Update to 4.4 which required a refactor of how all objects are created/destroyed using unique_ptr, we now use Cycles scene mutex to lock and create the initial objects instead of defer-creating and transferring in the render scene lock, which heavily simplifies the code.
  - Update to 4.5 now has lights as objects/geometry, simplifying code. Subdivision is a lot more stable with small refactor tweaks.
- Tractor : Ensure username is passed to the spooler
- GafferUI : OpenGL differences fix with the latest Qt.py
- Arnold : `ramp_rgb` and `ramp_float` support, with correct USD conversions
- GafferScene : Support M44f orientation conversions.

Fixes
-----


Breaking Changes
----------------
