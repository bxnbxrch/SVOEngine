#include "vox/scene/SceneBuffers.h"
#include "vox/graphics/VulkanDevice.h"
#include "vox/graphics/VulkanBuffer.h"
#include "vox/SparseVoxelOctree.h"

namespace vox {

SceneBuffers::SceneBuffers(VulkanDevice* device, const SparseVoxelOctree* octree)
    : m_device(device), m_octree(octree) {}

SceneBuffers::~SceneBuffers() = default;

bool SceneBuffers::init() {
    if (!m_octree) return false;

    const auto& nodes = m_octree->getNodes();
    const auto& colors = m_octree->getColors();

    // Create nodes buffer
    VkDeviceSize nodesSize = nodes.size() * sizeof(nodes[0]);
    m_nodesBuffer = std::make_unique<VulkanBuffer>(
        m_device,
        nodesSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    m_nodesBuffer->copyData(nodes.data(), nodesSize);

    // Create colors buffer
    VkDeviceSize colorsSize = colors.size() * sizeof(colors[0]);
    m_colorsBuffer = std::make_unique<VulkanBuffer>(
        m_device,
        colorsSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    m_colorsBuffer->copyData(colors.data(), colorsSize);

    m_initialized = true;
    return true;
}

} // namespace vox
