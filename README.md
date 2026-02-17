# vox — minimal Vulkan + SDL2 + GLM example

This project creates an SDL2 window and a Vulkan instance/surface, and demonstrates including GLM (header-only).

## Requirements (Linux)
- CMake >= 3.16
- a C++17 toolchain (g++/clang)
- libsdl2-dev
- libvulkan-dev (or Vulkan SDK)
- libglm-dev

On Debian/Ubuntu:

```bash
sudo apt update
sudo apt install build-essential cmake libsdl2-dev libvulkan-dev libglm-dev
```

## Build

```bash
mkdir build && cd build
cmake ..
make -j
./vox
```

The program opens a window; close it or press ESC to exit.

## Notes
- If CMake fails to find `glm`, install your distribution's `libglm-dev` (or use a package manager / vcpkg/Conan).
- This is intentionally minimal — rendering code is not included.
