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
mkdir -p asset/glsl
compile-time/build/rel/preprocess_shaders -t glsl -o asset/glsl/ shaders/model
compile-time/build/rel/preprocess_shaders -t glsl -o asset/glsl/ shaders/ui
compile-time/build/rel/preprocess_shaders -t glsl -o asset/glsl/ shaders/glyph
compile-time/build/rel/preprocess_shaders -t glsl -o asset/glsl/ shaders/contrast
compile-time/build/rel/preprocess_shaders -t glsl -o asset/glsl/ shaders/hblur
compile-time/build/rel/preprocess_shaders -t glsl -o asset/glsl/ shaders/vblur
compile-time/build/rel/preprocess_shaders -t glsl -o asset/glsl/ shaders/debug
compile-time/build/rel/preprocess_shaders -t glsl -o asset/glsl/ shaders/terrain
mkdir -p asset/glsl-es
compile-time/build/rel/preprocess_shaders -t glsl-es -o asset/glsl-es/ shaders/model
compile-time/build/rel/preprocess_shaders -t glsl-es -o asset/glsl-es/ shaders/ui
compile-time/build/rel/preprocess_shaders -t glsl-es -o asset/glsl-es/ shaders/glyph
compile-time/build/rel/preprocess_shaders -t glsl-es -o asset/glsl-es/ shaders/contrast
compile-time/build/rel/preprocess_shaders -t glsl-es -o asset/glsl-es/ shaders/hblur
compile-time/build/rel/preprocess_shaders -t glsl-es -o asset/glsl-es/ shaders/vblur
compile-time/build/rel/preprocess_shaders -t glsl-es -o asset/glsl-es/ shaders/debug
compile-time/build/rel/preprocess_shaders -t glsl-es -o asset/glsl-es/ shaders/terrain

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
build/debug/core/server --restart

