#!/bin/bash -e

CLAP_OPTS=""
[ -f $PWD/build_config ] && . $PWD/build_config
[ -n "$emsdk_env_path" ] &&	. $emsdk_env_path

if [ -n "$www_dir" ]; then
	www_dir="-DCMAKE_INSTALL_PREFIX=$www_dir"
fi

if [ -n "$use_gles" ]; then
	CLAP_OPTS="$CLAP_OPTS -DCLAP_BUILD_WITH_GLES=ON"
fi
if [ -n "$server_ip" ]; then
	CLAP_OPTS="$CLAP_OPTS -DCLAP_SERVER_IP=$server_ip -DCLAP_BUILD_NETWORKING=ON"
fi
cmake -B build/rel -DCMAKE_BUILD_TYPE=Release -DCLAP_BUILD_FINAL=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON $CLAP_OPTS
cmake -B build/test -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON $CLAP_OPTS
cmake -B build/debug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON $CLAP_OPTS

if which emcmake 1>/dev/null 2>&1; then
	emcmake cmake -B build/emrel -DCMAKE_BUILD_TYPE=Release -DCLAP_BUILD_FINAL=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON $CLAP_OPTS "$www_dir"
	emcmake cmake -B build/emtest -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON $CLAP_OPTS "$www_dir"
	emcmake cmake -B build/emdebug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON $CLAP_OPTS "$www_dir"
fi

