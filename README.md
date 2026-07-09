# NVIDIA OVRTX Maya Plugin

This is a C++ Maya plugin that integrates the NVIDIA Omniverse RTX (OVRTX) renderer with MayaUSD, to allow rendering path-traced USD scenes directly within Maya.

<img width="1602" height="1745" alt="New Project" src="https://github.com/user-attachments/assets/982706f0-c0b8-436b-9e41-d2493d2b7af1" />


OVRTX is a pre-release NVIDIA SDK that exposes the Omniverse RTX renderer through both C and Python APIs. This project is built primarily using the C API.

The OVRTX documentation includes plenty of examples that makes getting started straightforward:

https://nvidia-omniverse.github.io/ovrtx/index.html

The task-oriented agent skills in the OVRTX GitHub repository is also really helpful for learning the API and figuring out how different parts of the SDK fit together:

https://github.com/nvidia-omniverse/ovrtx

# Getting Started

The following instructions will help you get the project up and running on your local machine for development. At the moment, the project only supports Windows. If there's enough interest, I'd be happy to port it to MacOS and Linux in the future.

## Prerequisites and Dependencies

- C++ compiler with C++20 support (MSVC)
- Qt 6.x
- CMake 3.17 or higher
- Autodesk Maya && MayaUsd
- Nvidia OVRTX
- Pixar's OpenUSD

## Building

### Windows (Visual Studio 2022)

```
cd Nvidia-Ovrtx-Maya-Plugin
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
 -DUSD_INSTALL_PREFIX="path_to_openusd_install_directory" ^ 
 -DMAYAUSD_INSTALL_LOCATION="path_to_mayausd_install_directory" ^ 
 -DMAYA_INSTALL_LOCATION="path_to_maya_install_directory" 
 -DQT_LOCATION="path_to_qt_install_directory" 
 -DCMAKE_INSTALL_PREFIX="location_to_install_directory" ..

cmake --build . --config RelWithDebInfo --target install
```

## Loading Plugin

ovrtxMayaPlugin includes a Maya module (.mod) file. Add the installation directory to your MAYA_MODULE_PATH:

```
set MAYA_MODULE_PATH=%MAYA_MODULE_PATH%;<path_to_install_directory>
```
Launch Maya, then run the following MEL commands to load the plugin and show the render window:

```
loadPlugin "ovrtxMayaPlugin";
ovrtxRender;
```

## Supported features

The plugin currently supports:

- Anonymous layer
- Single stage
- Multiple stages
- MaterialX and UsdPreviewSurface

## Known Issues

- ovrtx_install_runtime() copies several ovrtx directories next to ovrtxMayaPlugin.mll. Together, they take up several gigabytes of disk space, with the precompiled shader cache alone being about 1 GB. This is expected and not a packaging issue. Apparently, all these directories are required at runtime. The RTX renderer loads each of them during startup, and removing any one of them prevents the HydraEngine from being created. At the moment, none of these directories can be omitted without breaking the renderer.

- In some rare cases, pressing Render multiple times can produce slightly different fully converged final images, even when the scene has not changed. This usually happens within the first 3 renders.

- OVRTX render view doesn't properly respect Resolution Gate && Film Gate sizes

## License

ovrtxMayaPlugin is licensed under the MIT License.
