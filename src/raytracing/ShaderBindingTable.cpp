#include "vox/raytracing/ShaderBindingTable.h"
#include "vox/graphics/VulkanDevice.h"
#include "vox/graphics/VulkanBuffer.h"
#include <iostream>
#include <cstring>

namespace vox {

ShaderBindingTable::ShaderBindingTable(VulkanDevice* device) : m_device(device) {}

ShaderBindingTable::~ShaderBindingTable() = default;

bool ShaderBindingTable::buildFromPipeline(VkPipeline rtPipeline) {
    // Get RTX pipeline properties
    const auto& props = m_device->rtPipelineProperties();
    uint32_t handleSize = props.shaderGroupHandleSize;
    uint32_t handleAlignment = props.shaderGroupHandleAlignment;
    uint32_t baseAlignment = props.shaderGroupBaseAlignment;
    
    uint32_t handleSizeAligned = (handleSize + handleAlignment - 1) & ~(handleAlignment - 1);
    
    uint32_t rgenStride = (handleSizeAligned + baseAlignment - 1) & ~(baseAlignment - 1);
    uint32_t missStride = handleSizeAligned;
    uint32_t hitStride = handleSizeAligned;
    
    uint32_t rgenSize = rgenStride;
    uint32_t missSize = missStride;
    uint32_t hitSize = hitStride;
    
    VkDeviceSize sbtSize = rgenSize + missSize + hitSize;
    
    // Get shader group handles (3 groups: raygen, miss, hit)
    std::vector<uint8_t> handleData(3 * handleSize);
    if (m_device->getRayTracingShaderGroupHandlesKHR(m_device->device(), rtPipeline, 0, 3, handleData.size(), handleData.data()) != VK_SUCCESS) {
        std::cerr << "Failed to get RT shader group handles\n";
        return false;
    }
    
    // Create SBT buffer with device address
    m_buffer = std::make_unique<VulkanBuffer>(
        m_device,
        sbtSize,
        VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    
    // Map and fill SBT
    void* sbtData = m_buffer->map();
    uint8_t* sbtBytes = reinterpret_cast<uint8_t*>(sbtData);
    
    // Copy handles: raygen (group 0), miss (group 1), hit (group 2)
    std::memcpy(sbtBytes, handleData.data(), handleSize);  // raygen
    std::memcpy(sbtBytes + rgenSize, handleData.data() + handleSize, handleSize);  // miss
    std::memcpy(sbtBytes + rgenSize + missSize, handleData.data() + 2 * handleSize, handleSize);  // hit
    
    m_buffer->unmap();
    
    // Get SBT buffer device address
    VkDeviceAddress sbtAddress = m_buffer->deviceAddress();
    
    // Setup SBT regions
    m_rgenRegion.deviceAddress = sbtAddress;
    m_rgenRegion.stride = rgenStride;
    m_rgenRegion.size = rgenSize;
    
    m_missRegion.deviceAddress = sbtAddress + rgenSize;
    m_missRegion.stride = missStride;
    m_missRegion.size = missSize;
    
    m_hitRegion.deviceAddress = sbtAddress + rgenSize + missSize;
    m_hitRegion.stride = hitStride;
    m_hitRegion.size = hitSize;
    
    m_callRegion.deviceAddress = 0;
    m_callRegion.stride = 0;
    m_callRegion.size = 0;
    
    std::cout << "✓ Shader binding table created\n";
    return true;
}

} // namespace vox
