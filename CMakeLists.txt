cmake_minimum_required(VERSION 3.16)
project(vox-vk-sdl-glm VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL2 REQUIRED)
find_package(Vulkan REQUIRED)
find_package(glm REQUIRED)

include(cmake/CompileShaders.cmake)

set(SHADER_SRC_DIR "${CMAKE_SOURCE_DIR}/shaders")
set(SHADER_OUT_DIR "${CMAKE_BINARY_DIR}/shaders")
compile_shaders(${SHADER_OUT_DIR} ${SHADER_SRC_DIR}
  raytrace.comp
  raytrace.rgen
  raytrace.rchit
  raytrace.rmiss
  raytrace.rint
)
# COMPILED_SHADER_SPVS is populated by compile_shaders()

add_executable(vox
  src/main.cpp
  src/Application.cpp
  src/Window.cpp
  src/VulkanRendererCore.cpp
  src/VulkanRendererInit.cpp
  src/VulkanRendererSwapchain.cpp
  src/VulkanRendererDraw.cpp
  src/Shader.cpp
  src/SparseVoxelOctree.cpp
  src/graphics/VulkanDevice.cpp
  src/graphics/VulkanBuffer.cpp
  src/graphics/VulkanImage.cpp
  src/graphics/VulkanShader.cpp
  src/graphics/ComputePipeline.cpp
  src/graphics/RayTracingPipeline.cpp
  src/graphics/DescriptorSet.cpp
  src/scene/Camera.cpp
  src/scene/Scene.cpp
  src/scene/SceneBuffers.cpp
  src/render/SceneRenderer.cpp
  src/render/ComputeSceneRenderer.cpp
  src/render/RayTracingSceneRenderer.cpp
  src/raytracing/AccelerationStructure.cpp
  src/raytracing/ShaderBindingTable.cpp
  ${COMPILED_SHADER_SPVS}
)

target_compile_definitions(vox PRIVATE SDL_MAIN_HANDLED GLM_ENABLE_EXPERIMENTAL)
target_compile_options(vox PRIVATE -Wall -Wextra -Wpedantic)

# Control DBGPRINT via CMake option (default: OFF)
option(ENABLE_DBGPRINT "Enable DBGPRINT debug output" OFF)
if(ENABLE_DBGPRINT)
  message(STATUS "ENABLE_DBGPRINT=ON: defining DEBUG")
  target_compile_definitions(vox PRIVATE DEBUG)
endif()
# expose public headers
target_include_directories(vox PRIVATE ${CMAKE_SOURCE_DIR}/include)

if (TARGET SDL2::SDL2)
  target_link_libraries(vox PRIVATE SDL2::SDL2 Vulkan::Vulkan glm::glm)
else()
  target_include_directories(vox PRIVATE ${SDL2_INCLUDE_DIRS})
  target_link_libraries(vox PRIVATE ${SDL2_LIBRARIES} Vulkan::Vulkan glm::glm)
endif()
