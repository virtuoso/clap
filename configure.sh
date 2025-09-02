#!/bin/bash -e

CLAP_OPTS=""
[ -f $PWD/build_config ] && . $PWD/build_config
[ -n "$emsdk_env_path" ] &&	. $emsdk_env_path

if [ -n "$www_dir" ]; then
	export WWW_INSTALL_DIR="$www_dir"
	www_dir="-DCMAKE_INSTALL_PREFIX=$www_dir"
fi

if [ -n "$use_gles" ]; then
	CLAP_OPTS="$CLAP_OPTS -DCLAP_BUILD_WITH_GLES=ON"
fi
if [ -n "$server_ip" ]; then
	CLAP_OPTS_NATIVE="$CLAP_OPTS -DCLAP_SERVER_IP=$server_ip -DCLAP_BUILD_NETWORKING=ON"
fi

cmake --preset=rel $CLAP_OPTS_NATIVE
cmake --preset=test $CLAP_OPTS_NATIVE
cmake --preset=debug $CLAP_OPTS_NATIVE

if which emcmake 1>/dev/null 2>&1; then
	cmake --preset=emrel $CLAP_OPTS "$www_dir"
	cmake --preset=emtest $CLAP_OPTS "$www_dir"
	cmake --preset=emdebug $CLAP_OPTS "$www_dir"
fi

