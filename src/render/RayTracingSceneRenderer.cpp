#include "vox/render/RayTracingSceneRenderer.h"
#include "vox/graphics/RayTracingPipeline.h"
#include "vox/graphics/VulkanDevice.h"
#include <iostream>

namespace vox {

RayTracingSceneRenderer::RayTracingSceneRenderer(VulkanDevice* device, Scene* scene)
    : SceneRenderer(device, scene) {}

bool RayTracingSceneRenderer::selectPipeline() {
    if (!m_device->supportsRayTracing()) {
        std::cerr << "Ray tracing not supported on this GPU\n";
        return false;
    }
    m_pipeline = std::make_unique<RayTracingPipeline>(m_device);
    return true;
}

} // namespace vox
