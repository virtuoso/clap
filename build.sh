#!/bin/bash -e

VERBOSE=""
if [ -n "$1" ]; then
	VERBOSE="--verbose"
fi
cmake --build build/rel $VERBOSE
cmake --build build/debug $VERBOSE
cmake --build build/emrel $VERBOSE
cmake --build build/emdebug $VERBOSE
cmake --install build/emrel $VERBOSE
cmake --install build/emdebug $VERBOSE
#cp build/emrel/core/onehandclap.{data,html,js,wasm} /var/www/html/cclap
build/rel/core/onehandclap --restart

