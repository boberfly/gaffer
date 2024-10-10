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
  - Refactored the python module so that the data needed for binding is all located in IECoreCycles and not dependent on linking with Cycles itself for that data.
  - Added `majorVersion` `minorVersion` `patchVersion` and `version` to the python module to easily query the running Cycles version
  - OpenVDBs will now render and allow shader updates in a live render.
- Tractor : Ensure username is passed to the spooler

Fixes
-----


Breaking Changes
----------------
