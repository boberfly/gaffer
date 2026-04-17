#!/bin/bash

# First run something like build_usd2511.bat --variants 6 -s in rez-package to set the environment:
# Run the resulting build-env rez gives you:
# /opt/workspace/rpks/gaffer/1.7.0.0/build/ac69120bbabc1777985bb535ccc63bee4153f150/build-env
# also consider passing BUILD_TYPE BUILD_DIR BUILD_CACHEDIR CXXSTD INSTALL_DIR or SAVE_OPTIONS=custom.options
# to the arguments of this script

scons -j $(nproc) OPTIONS=mnraker.options $@
scons install -j $(nproc) OPTIONS=mnraker.options $@
