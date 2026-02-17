#include "vox/scene/Scene.h"
#include "vox/scene/SceneBuffers.h"
#include "vox/SparseVoxelOctree.h"
#include "vox/graphics/VulkanDevice.h"

namespace vox {

Scene::Scene(VulkanDevice* device) : m_device(device) {
    m_octree = std::make_unique<SparseVoxelOctree>(8);
    m_buffers = std::make_unique<SceneBuffers>(device, m_octree.get());
}

Scene::~Scene() = default;

bool Scene::loadVoxelFile(const std::string& path) {
    if (!m_octree->loadFromVoxFile(path)) {
        return false;
    }
    return initGPUBuffers();
}

bool Scene::initGPUBuffers() {
    if (!m_buffers->init()) {
        return false;
    }
    return true;
}

} // namespace vox
