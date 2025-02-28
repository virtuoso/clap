# Configuring Visual Studio Code

- [Native build/debug](#native-builddebug)
  - [settings.json](#settingsjson)
  - [tasks.json](#tasksjson)
  - [launch.json](#launchjson)
- [Wasm debug using Chrome](#wasm-debug-using-chrome)

Here's a collection of copy-pastable Visual Studio Code config snippets to quickly get up and running with building and debugging clap demo (ldjam56). Please report and/or send fixes if something is wrong or you know of better ways of doing things (like, demo/ldjam56 is hardcoded in some places). Some settings are platform-specific, they'll be in their respective subchapters.

First, you'll need to install the following extensions:
- ms-vscode.cpptools
- ms-vscode.cmake-tools

The latter adds a tab to the left side panel thingy where you can set the current cmake target. This is useful for working on one target at a time, because the alternative ("Build all") builds all targets, which even on massively multicore systems will run into long linking stage for each target.

Additionally, if you're planning to debug wasm builds, also these:
- ms-vscode.wasm-dwarf-debugging
- ms-vscode.live-server

(see [this post by floooh](https://floooh.github.io/2023/11/11/emscripten-ide.html) on the basic setup; some things have changed slightly since that was written, but the bulk of it still holds up).

You'll need 3 files in the local **.vscode** directory: **settings.json**, **tasks.json** and **launch.json**.

> [!NOTE]
> You're responsible for correctness of your build configs. If the below sets your machine on fire, you can only blame yourself.

## Native build/debug

### settings.json

> [!WARNING]
> You'll need to change the value of "cmake.parallelJobs" to what your machine can handle without contention for CPU time, probably the number of cores.

> [!NOTE]
> Parallel build speeds up builds a lot if you change cmake files or one of the header files that is included everywhere, so do make use of this option.

**.vscode/settings.json**
```json
{
    "[c]": {
        "editor.tabSize": 4,
        "editor.useTabStops": false,
    },
    "[c++]": {
        "editor.tabSize": 4,
        "editor.useTabStops": false,
    },
    "editor.renderWhitespace":"always",
    "editor.formatOnType": false,
    "editor.formatOnPaste": false,
    "editor.formatOnSave": false,
    "C_Cpp.default.compileCommands": "${workspaceFolder}/build/${command:cmake.configurePreset}/compile_commands.json",
    "C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools",
    "cmake.configureOnOpen": false,
    "cmake.parallelJobs": 4,
    "cmake.useCMakePresets": "always",
    "cmake.exportCompileCommandsFile": true,
    "cmake.buildDirectory": "${workspaceFolder}/build/${command:cmake.configurePreset}"
}
```

### tasks.json

> [!NOTE]
> Set EMSDK to the path where emscripten is installed on your system (probably in your home directory somewhere), same as "emsdk_env_path" in **build_config**, see [Wasm build](https://github.com/virtuoso/clap?tab=readme-ov-file#wasm-build).

> [!NOTE]
> Set WWW_INSTALL_DIR to the install path, same as "www_dir" in **build_config**, see [Wasm build](https://github.com/virtuoso/clap?tab=readme-ov-file#wasm-build).

The below configures two main build tasks:
* "Build all": runs ./build.sh (depends on ./configure.sh to create cmake caches that you need to run manually at least once)
* "CMake: configure": runs the currently configured cmake target (F7 to build, F5 to build and launch debugger).

**.vscode/tasks.json**
```json
{
    "version": "2.0.0",
    "options": {
        "env": {
            "EMSDK": "/home/.../emsdk",
            "WWW_INSTALL_DIR": "/home/.../public_html"
        }
    },
    "tasks": [
        {
            "label": "Build all",
            "type": "shell",
            "command": "./build.sh",
            "problemMatcher": [
                "$gcc"
            ],
            "group": {
                "kind": "build",
                "isDefault": true
            }
        },
        {
            "type": "cmake",
            "label": "CMake: configure",
            "command": "configure",
            "preset": "${command:cmake.activeConfigurePresetName}",
            "problemMatcher": [],
            "detail": "CMake configure task",
            "presentation": {
                "echo": true,
                "reveal": "always",
                "focus": true,
                "panel": "shared",
                "showReuseMessage": true,
                "clear": true
            },
            "group":"build"
        }
    ]
}
```

### launch.json

This one will be different depending on the platform.

#### Linux

This only works if you have a working **gdb**, that is, on Linux.

**.vscode/launch.json**
```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "gcc build and debug",
            "type": "cppdbg",
            "request": "launch",
            "program": "${command:cmake.buildDirectory}/demo/ldjam56/${command:cmake.launchTargetFilename}",
            "args": [ "-E" ],
            "stopAtEntry": false,
            "cwd": "${workspaceFolder}",
            "environment": [{
                "name": "DISPLAY",
                "value": ":0"
            }],
            "externalConsole": false,
            "MIMode": "gdb",
            "setupCommands": [
                {
                    "description": "Enable pretty-printing for gdb",
                    "text": "-enable-pretty-printing",
                    "ignoreFailures": true
                }
            ],
            "preLaunchTask": "Build native",
            "miDebuggerPath": "/usr/bin/gdb"
        }
    ]
}
```

#### Mac OS X

I have not tried lldb on Linux, the below may or may not work on Linux as well. Having used gdb for decades, I find lldb's command interface differs from gdb's just enough to make using it extremely irritating. You don't have to worry about this as long as you're using it from vscode.

> [!NOTE]
> The only way to get the console output on Mac OS X is to launch the external terminal (the "externalConsole" option below). The problem is that it will launch a new instance or a new tab of the terminal every time, which is tiresome, so it's commented out below.

**.vscode/launch.json**
```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Launch native",
            "type": "cppdbg",
            "request":"launch",
            "cwd": "${workspaceFolder}",
            "args": [ "-E" ],
            "MIMode": "lldb",
            // "externalConsole": true,
            "program": "${command:cmake.buildDirectory}/demo/ldjam56/${command:cmake.launchTargetFilename}",
        }
    ]
}
```

## Wasm debug using Chrome

Add the following to the "configurations" array in **.vscode/launch.json**:
```json
{
    "name": "Launch chrome",
    "type": "chrome",
    "runtimeExecutable": "chrome",
    "request": "launch",
    "url": "http://localhost:3000/build/${command:cmake.activeConfigurePresetName}/demo/ldjam56/${command:cmake.launchTargetFilename}",
    "preLaunchTask": "StartServer",
}
```
> [!NOTE]
> You can use Chrome Canary here too, set the value of "runtimeExecutable" to "canary".

Then, the following to "tasks" array in **.vscode/tasks.json**:
```json
{
    "label": "StartServer",
    "type": "process",
    "command": "${input:startServer}"
},
```

And finally, the following to the top level object in **.vscode/tasks.json**:
```json
"inputs": [
    {
        "id": "startServer",
        "type": "command",
        "command": "livePreview.runServerLoggingTask"
    }
]
```

With these in place, you should be able to select "emdebug" cmake target in the cmake panel in vscode and F5 should launch a web server and an instance of chrome and point it to the built index.html, at which point, regular source-level debugging from vscode should Just Work (tm).
