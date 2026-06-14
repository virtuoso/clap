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

CLAP_OPTS_NET=""
if [ -n "$server_ip" ]; then
	CLAP_OPTS_NET="-DCLAP_SERVER_IP=$server_ip -DCLAP_BUILD_NETWORKING=ON"
fi
if [ -n "$server_port" ]; then
	CLAP_OPTS_NET="$CLAP_OPTS_NET -DCLAP_SERVER_PORT=$server_port"
fi
if [ -n "$server_wsport" ]; then
	CLAP_OPTS_NET="$CLAP_OPTS_NET -DCLAP_SERVER_WSPORT=$server_wsport"
fi

# release builds ship to people and must not dial anybody's server:
# no networking opts for rel/emrel, and their presets pin it off
cmake --preset=rel $CLAP_OPTS
cmake --preset=test $CLAP_OPTS $CLAP_OPTS_NET
cmake --preset=debug $CLAP_OPTS $CLAP_OPTS_NET

if which emcmake 1>/dev/null 2>&1; then
	cmake --preset=emrel $CLAP_OPTS "$www_dir"
	cmake --preset=emtest $CLAP_OPTS $CLAP_OPTS_NET "$www_dir"
	cmake --preset=emdebug $CLAP_OPTS $CLAP_OPTS_NET "$www_dir"
fi

