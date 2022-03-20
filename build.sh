#!/bin/bash -e

VERBOSE=""
if [ -n "$1" ]; then
	VERBOSE="--verbose"
fi

cd compile-time
cmake -B build/rel -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/rel
cd ..
mkdir -p asset/glsl
compile-time/build/rel/preprocess_shaders -t glsl -o asset/glsl/ shaders/model
compile-time/build/rel/preprocess_shaders -t glsl -o asset/glsl/ shaders/ui
compile-time/build/rel/preprocess_shaders -t glsl -o asset/glsl/ shaders/glyph
mkdir -p asset/glsl-es
compile-time/build/rel/preprocess_shaders -t glsl-es -o asset/glsl-es/ shaders/model
compile-time/build/rel/preprocess_shaders -t glsl-es -o asset/glsl-es/ shaders/ui
compile-time/build/rel/preprocess_shaders -t glsl-es -o asset/glsl-es/ shaders/glyph

cmake --build build/rel $VERBOSE
cmake --build build/debug $VERBOSE
cmake --build build/emrel $VERBOSE
cmake --build build/emdebug $VERBOSE
cmake --install build/emrel $VERBOSE
cmake --install build/emdebug $VERBOSE

#cp build/emrel/core/onehandclap.{data,html,js,wasm} /var/www/html/cclap
build/rel/core/onehandclap --restart

