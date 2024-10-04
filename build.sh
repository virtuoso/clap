#!/bin/bash -e

[ -f $PWD/build_config ] && . $PWD/build_config

OPTS="$(echo $opts)"
VERBOSE=""
if [ -n "$1" ]; then
	VERBOSE="--verbose"
fi

cd compile-time
cmake -B build/rel -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/rel
cd ..

cmake --build build/rel $VERBOSE $OPTS
cmake --build build/test $VERBOSE $OPTS
cmake --build build/debug $VERBOSE $OPTS
make test -C build/debug
if [ -n "$www_dir" ]; then
	cmake --build build/emrel $VERBOSE $OPTS
	cmake --build build/emtest $VERBOSE $OPTS
	cmake --build build/emdebug $VERBOSE $OPTS
	cmake --install build/emrel $VERBOSE
	cmake --install build/emtest $VERBOSE
	cmake --install build/emdebug $VERBOSE
fi

#cp build/emrel/core/onehandclap.{data,html,js,wasm} /var/www/html/cclap
build/debug/tools/server/server --restart

