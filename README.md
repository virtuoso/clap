# CLAP game engine

![simple-build](https://github.com/virtuoso/clap/actions/workflows/simple-build.yml/badge.svg?branch=main)
![macos-build](https://github.com/virtuoso/clap/actions/workflows/macos-build.yml/badge.svg?branch=main)
![windows-build](https://github.com/virtuoso/clap/actions/workflows/windows-build.yml/badge.svg?branch=main)

## Source code and submodules

Checkout repository and modules

```sh
git submodule update --init --recursive
```

## Local build (Macos)

### Install dependencies

```sh
brew install cmake glew zlib libpng libogg libvorbis freetype glfw shaderc spirv-cross
```

### Build

```sh
./configure.sh
./build.sh
```

### Run

Optionally, you can run a server that collects logs from all instances of the engine client for debugging purposes. If you don't specify "server_ip" in the build_config configuration file, or don't specify -DCLAP_BUILD_NETWORKING during cmake configuration stage, you don't have to run the server. Otherwise, set "server_ip" to the IP address of the machine where the server will be running.

```sh
build/debug/tools/server/server
```

And run the demo

```sh
build/test/demo/ldjam56/ldjam56
```

## Wasm build

Install all dependencies needed for native build

### Emscripten

Download and install [Emscripten](https://emscripten.org/docs/getting_started/downloads.html)

### Create build_config file

```sh
emsdk_env_path=<path to installed emscripten SDK>
www_dir=<path to www build folder where static content will be build>
```
See [Run](#run) section about the server_ip=... setting.

For example:

```sh
emsdk_env_path=${HOME}/src/game/emsdk/emsdk_env.sh
www_dir=${HOME}/src/game/www
```

### Run local web server

For example, using python3 embedded httpd server:

```sh
python3 -m http.server -d ${HOME}/src/game/www
```

## Linux build

Install dependencies (see also the simple-build.yml in case this goes out of date):
```sh
sudo apt-get install -y libfreetype-dev libglew-dev libglfw3-dev libogg-dev libopenal-dev libpng-dev libvorbis-dev zlib1g-dev glslc spirv-cross
```

The rest is the same as the [Mac OS build instructions](#local-build-macos)

## Windows build

Ok, are you sure? Yes? Read on.
For reference, check [windows-build github action](https://github.com/virtuoso/clap/blob/main/.github/workflows/windows-build.yml).

First, install [Visual Studio 2019](https://visualstudio.microsoft.com/vs/older-downloads/) Community edition (free). VS 2022 doesn't work yet, because of conflicts with windows-libc, somebody will fix it eventually.

Then, install [LLVM](https://releases.llvm.org/download.html) for clang. There's also a way of installing clang via Visual Studio, but this one is easier.

Then, install [python](https://www.python.org/downloads/windows/).

Then, set up vcpkg and set an environment variable to the path of the cloned repository:
```sh
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat -disableMetrics
export VCPKG_INSTALLATION_ROOT=.../vcpkg
```

Note: everything up to this point is preinstalled in github runners, so windows-build.yml doesn't mention any of that.

Then, install all the dependencies using vcpkg:
```sh
$VCPKG_INSTALLATION_ROOT/vcpkg.exe libpng:x64-windows-static glfw3:x64-windows-static glew:x64-windows-static freetype:x64-windows-static openal-soft:x64-windows-static libogg:x64-windows-static libvorbis:x64-windows-static shaderc spirv-cross
```

Then, in the root of the clap directory, configure and build using cmake. Use --parallel X option with cmake --build commands to speed things up.
```sh
cmake --preset w32test -B build/test
cmake --preset w32debug -B build/debug
cmake --build build/test
cmake --build build/debug
```

Then, run the result:
```sh
.\build\test\demo\ldjam56\ldjam56.exe
```
