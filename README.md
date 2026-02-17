# vox svo engine

voxel ray tracing with vulkan sdl2 and glm

## windows

requirements
- cmake 3 16+
- visual studio 2019 or 2022 with c++ desktop workload
- vulkan sdk
- sdl2 dev package
- glm

install stuff
- install the vulkan sdk from lunarg and reboot
- sdl2 and glm via vcpkg is the easiest

example with vcpkg
```bash
git clone https://github.com/microsoft/vcpkg
cd vcpkg
bootstrap-vcpkg.bat
vcpkg install sdl2 glm
```

build and run
```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config release
build/Release/vox.exe
```

## ubuntu

requirements
- cmake 3 16+
- g++ or clang
- sdl2 dev
- vulkan dev
- glm

install
```bash
sudo apt update
sudo apt install build-essential cmake libsdl2-dev libvulkan-dev libglm-dev
```

build and run
```bash
cmake -S . -B build
cmake --build build -j
./build/vox
```

## arch

requirements
- cmake
- gcc or clang
- sdl2
- vulkan loader and headers
- glm

install
```bash
sudo pacman -S --needed base-devel cmake sdl2 vulkan-headers vulkan-loader glm
```

build and run
```bash
cmake -S . -B build
cmake --build build -j
./build/vox
```

## mac

requirements
- cmake
- xcode command line tools
- moltenvk via vulkan sdk
- sdl2
- glm

install
```bash
xcode-select --install
brew install cmake sdl2 glm
```

vulkan on mac
- install the vulkan sdk and make sure vulkan loader and moltenvk are available

build and run
```bash
cmake -S . -B build
cmake --build build -j
./build/vox
```

## notes
- if cmake cant find glm or sdl2 use vcpkg or set CMAKE_PREFIX_PATH
- on mac you need moltenvk from the vulkan sdk
- runtime data expects monu1.vox in the project root
