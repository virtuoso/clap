#!/bin/bash

pwd
[ -f $PWD/build_config ] && . $PWD/build_config
[ -n "$emsdk_env_path" ] && . $emsdk_env_path
if [ -n "$EMSDK" ]; then
    echo "Found emsdk in the environment"
    www_build=1
fi

make
make DEBUG=1 BUILDROOT=build/debug
if [ -n "$www_build" ]; then
    make BUILD=www all DESTDIR="$www_dir"
    if [ -n "$wwwdbg_dir" ]; then
        mkdir -p "$wwwdbg_dir"
        make BUILD=www all DEBUG=1 BUILDROOT=build/debug DESTDIR="$wwwdbg_dir"
    else
        make BUILD=www all DEBUG=1 BUILDROOT=build/debug
    fi
    if [ -n "$www_dir" ]; then
	    cp -a asset "$www_dir"
    fi
    if [ -n "$wwwdbg_dir" ]; then
	    cp -a asset "$wwwdbg_dir"
	    cp *.c *.h "$wwwdbg_dir"
    fi
    if [ -n "$upload_host" -a -n "$upload_path" ]; then
        echo "Uploading"
        # not copying asset directory as it's already in .data file
        tar zcf - onehandclap.{html,wasm,wasm.map,data,js} *.c *.h | ssh "$upload_host" "cd $upload_path && tar zxf -"
    fi
fi

build/debug/bin/onehandclap --restart

