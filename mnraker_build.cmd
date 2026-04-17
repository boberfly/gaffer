rem First run something like build_usd2511.bat --variants 6 -s in rez-package to set the environment:
rem Run the resulting build-env rez gives you:
rem C:\dev\rpks\gaffer\1.7.0.0\build\ac69120bbabc1777985bb535ccc63bee4153f150\build-env
rem also consider passing BUILD_TYPE BUILD_DIR BUILD_CACHEDIR CXXSTD INSTALL_DIR or SAVE_OPTIONS=custom.options
rem to the arguments of this script

call scons -j %NUMBER_OF_PROCESSORS% OPTIONS=mnraker.options %*
call scons install -j %NUMBER_OF_PROCESSORS% OPTIONS=mnraker.options %*
