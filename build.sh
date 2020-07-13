#!/bin/bash

pwd
[ -f $PWD/build_config ] && . $PWD/build_config
[ -n "$emsdk_env_path" ] && . $emsdk_env_path
if [ -n "$EMSDK" ]; then
    echo "Found emsdk in the environment"
    www_build=1
fi

make
if [ -n "$www_build" ]; then
    make BUILD=www all DESTDIR="$www_dir"
    cp -a asset "$www_dir"
    cp *.c *.h "$www_dir"
fi
