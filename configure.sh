#!/bin/bash -e

[ -f $PWD/build_config ] && . $PWD/build_config
[ -n "$emsdk_env_path" ] &&	. $emsdk_env_path

if [ -n "$www_dir" ]; then
	www_dir="-DCMAKE_INSTALL_PREFIX=$www_dir"
fi

cmake -B build/rel -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake -B build/debug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

if which emcmake 1>/dev/null 2>&1; then
	emcmake cmake -B build/emrel -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON "$www_dir"
	emcmake cmake -B build/emdebug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON "$www_dir"
fi

