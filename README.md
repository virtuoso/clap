# CLAP game engine

![simple-build](https://github.com/virtuoso/clap/actions/workflows/simple-build.yml/badge.svg?branch=main)

## Source code and submodules

Checkout repository and modules

```sh
git submodule update --init --recursive
```

## Local build (Macos)

### Install dependencies

```sh
brew install glew zlib libpng libogg freetype glfw
```

### Build

```sh
./configure
./build.sh
```

### Run

```sh
build/debug/tools/server/server
```

And run the demo

```sh
build/test/demo/ldjam52/ldjam52
```

## Wasm build

Install all dependencies needed for native build

### Emscripten

Download and install [Emscripten](https://emscripten.org/docs/getting_started/downloads.html)

### Create build_config file

```properties
emsdk_env_path=<path to installed emscripten SDK>
www_dir=<path to www build folder where static content will be build>
```

### Run local web server

For install python3 embedded httpd server

```sh
python3 -m http.server -d <path to the www folder>
```
