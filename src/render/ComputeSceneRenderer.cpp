#include "vox/render/ComputeSceneRenderer.h"
#include "vox/graphics/ComputePipeline.h"
#include "vox/graphics/VulkanDevice.h"

namespace vox {

ComputeSceneRenderer::ComputeSceneRenderer(VulkanDevice* device, Scene* scene)
    : SceneRenderer(device, scene) {}

bool ComputeSceneRenderer::selectPipeline() {
    m_pipeline = std::make_unique<ComputePipeline>(m_device);
    return true;
}

} // namespace vox
