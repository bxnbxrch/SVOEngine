#include "vox/raytracing/AccelerationStructure.h"
#include "vox/graphics/VulkanDevice.h"
#include "vox/graphics/VulkanBuffer.h"
#include <iostream>

namespace vox {

AccelerationStructure::AccelerationStructure(VulkanDevice* device, bool isTopLevel)
    : m_device(device), m_isTopLevel(isTopLevel) {}

AccelerationStructure::~AccelerationStructure() {
    if (m_accelerationStructure != VK_NULL_HANDLE) {
        m_device->destroyAccelerationStructureKHR(m_device->device(), m_accelerationStructure, nullptr);
    }
}

VkDeviceAddress AccelerationStructure::deviceAddress() const {
    if (!m_accelerationStructure) return 0;
    VkAccelerationStructureDeviceAddressInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    info.accelerationStructure = m_accelerationStructure;
    return m_device->getAccelerationStructureDeviceAddressKHR(m_device->device(), &info);
}

bool AccelerationStructure::buildBLAS(const std::vector<float>& aabbData) {
    if (aabbData.empty() || aabbData.size() % 6 != 0) {
        std::cerr << "Invalid AABB data for BLAS\n";
        return false;
    }
    
    uint32_t aabbCount = aabbData.size() / 6;
    
    // Create AABB buffer
    VkDeviceSize aabbBufferSize = aabbCount * sizeof(VkAabbPositionsKHR);
    auto aabbBuffer = std::make_unique<VulkanBuffer>(
        m_device,
        aabbBufferSize,
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    
    // Fill AABB buffer
    void* aabbPtr = aabbBuffer->map();
    auto* aabbs = reinterpret_cast<VkAabbPositionsKHR*>(aabbPtr);
    for (uint32_t i = 0; i < aabbCount; ++i) {
        aabbs[i].minX = aabbData[i * 6 + 0];
        aabbs[i].minY = aabbData[i * 6 + 1];
        aabbs[i].minZ = aabbData[i * 6 + 2];
        aabbs[i].maxX = aabbData[i * 6 + 3];
        aabbs[i].maxY = aabbData[i * 6 + 4];
        aabbs[i].maxZ = aabbData[i * 6 + 5];
    }
    aabbBuffer->unmap();
    
    // Setup BLAS build info
    VkAccelerationStructureGeometryAabbsDataKHR aabbsData{};
    aabbsData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
    aabbsData.data.deviceAddress = aabbBuffer->deviceAddress();
    aabbsData.stride = sizeof(VkAabbPositionsKHR);
    
    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
    geometry.geometry.aabbs = aabbsData;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    
    // Get build sizes
    VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo{};
    buildSizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    m_device->getAccelerationStructureBuildSizesKHR(
        m_device->device(),
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo,
        &aabbCount,
        &buildSizeInfo
    );
    
    // Create acceleration structure buffer
    m_buffer = std::make_unique<VulkanBuffer>(
        m_device,
        buildSizeInfo.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    
    // Create scratch buffer for build
    auto scratchBuffer = std::make_unique<VulkanBuffer>(
        m_device,
        buildSizeInfo.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    
    // Create acceleration structure
    VkAccelerationStructureCreateInfoKHR accelCreateInfo{};
    accelCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    accelCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    accelCreateInfo.size = buildSizeInfo.accelerationStructureSize;
    accelCreateInfo.buffer = m_buffer->buffer();
    accelCreateInfo.offset = 0;
    
    if (m_device->createAccelerationStructureKHR(m_device->device(), &accelCreateInfo, nullptr, &m_accelerationStructure) != VK_SUCCESS) {
        std::cerr << "Failed to create BLAS\n";
        return false;
    }
    
    buildInfo.dstAccelerationStructure = m_accelerationStructure;
    buildInfo.scratchData.deviceAddress = scratchBuffer->deviceAddress();
    
    // Store scratch buffer to keep it alive during build
    m_scratchBuffer = std::move(scratchBuffer);
    
    // Create a copy of geometry for the build call
    VkAccelerationStructureGeometryKHR geometryCopy = geometry;
    VkAccelerationStructureBuildGeometryInfoKHR buildInfoCopy = buildInfo;
    buildInfoCopy.pGeometries = &geometryCopy;
    
    // Create build range info
    VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
    buildRangeInfo.primitiveCount = aabbCount;
    buildRangeInfo.primitiveOffset = 0;
    buildRangeInfo.firstVertex = 0;
    buildRangeInfo.transformOffset = 0;
    
    const VkAccelerationStructureBuildRangeInfoKHR* pBuildRangeInfo = &buildRangeInfo;
    
    // Submit synchronous build
    if (m_device->buildAccelerationStructuresKHR(m_device->device(), VK_NULL_HANDLE, 1, &buildInfoCopy, &pBuildRangeInfo) != VK_SUCCESS) {
        std::cerr << "Failed to build BLAS\n";
        return false;
    }
    
    std::cout << "✓ BLAS built with " << aabbCount << " AABBs\n";
    return true;
}

bool AccelerationStructure::buildTLAS(VkAccelerationStructureKHR blas) {
    if (blas == VK_NULL_HANDLE) {
        std::cerr << "Invalid BLAS handle for TLAS\n";
        return false;
    }
    
    // Create instance buffer (single instance for octree BLAS)
    auto instanceBuffer = std::make_unique<VulkanBuffer>(
        m_device,
        sizeof(VkAccelerationStructureInstanceKHR),
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    
    // Fill instance with BLAS reference and identity transform
    void* instancePtr = instanceBuffer->map();
    auto* instance = reinterpret_cast<VkAccelerationStructureInstanceKHR*>(instancePtr);
    
    instance->transform.matrix[0][0] = 1.0f; instance->transform.matrix[0][1] = 0.0f; instance->transform.matrix[0][2] = 0.0f; instance->transform.matrix[0][3] = 0.0f;
    instance->transform.matrix[1][0] = 0.0f; instance->transform.matrix[1][1] = 1.0f; instance->transform.matrix[1][2] = 0.0f; instance->transform.matrix[1][3] = 0.0f;
    instance->transform.matrix[2][0] = 0.0f; instance->transform.matrix[2][1] = 0.0f; instance->transform.matrix[2][2] = 1.0f; instance->transform.matrix[2][3] = 0.0f;
    
    instance->instanceCustomIndex = 0;
    instance->mask = 0xFF;
    instance->instanceShaderBindingTableRecordOffset = 0;
    instance->flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    
    // Get BLAS device address for reference
    VkAccelerationStructureDeviceAddressInfoKHR blasAddrInfo{};
    blasAddrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    blasAddrInfo.accelerationStructure = blas;
    instance->accelerationStructureReference = m_device->getAccelerationStructureDeviceAddressKHR(m_device->device(), &blasAddrInfo);
    
    instanceBuffer->unmap();
    
    VkAccelerationStructureGeometryInstancesDataKHR instances{};
    instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    instances.data.deviceAddress = instanceBuffer->deviceAddress();
    
    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances = instances;
    
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    
    // Get build sizes
    uint32_t instanceCount = 1;
    VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo{};
    buildSizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    m_device->getAccelerationStructureBuildSizesKHR(
        m_device->device(),
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo,
        &instanceCount,
        &buildSizeInfo
    );
    
    // Create scratch buffer for build
    auto scratchBuffer = std::make_unique<VulkanBuffer>(
        m_device,
        buildSizeInfo.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    
    // Create TLAS buffer
    m_buffer = std::make_unique<VulkanBuffer>(
        m_device,
        buildSizeInfo.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    
    // Create acceleration structure
    VkAccelerationStructureCreateInfoKHR accelCreateInfo{};
    accelCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    accelCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    accelCreateInfo.size = buildSizeInfo.accelerationStructureSize;
    accelCreateInfo.buffer = m_buffer->buffer();
    accelCreateInfo.offset = 0;
    
    if (m_device->createAccelerationStructureKHR(m_device->device(), &accelCreateInfo, nullptr, &m_accelerationStructure) != VK_SUCCESS) {
        std::cerr << "Failed to create TLAS\n";
        return false;
    }
    
    buildInfo.dstAccelerationStructure = m_accelerationStructure;
    buildInfo.scratchData.deviceAddress = scratchBuffer->deviceAddress();
    
    // Store buffers to keep them alive during build
    m_instanceBuffer = std::move(instanceBuffer);
    m_scratchBuffer = std::move(scratchBuffer);
    
    // Create a copy of geometry for the build call
    VkAccelerationStructureGeometryKHR geometryCopy = geometry;
    VkAccelerationStructureBuildGeometryInfoKHR buildInfoCopy = buildInfo;
    buildInfoCopy.pGeometries = &geometryCopy;
    
    // Create build range info
    VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
    buildRangeInfo.primitiveCount = 1;
    buildRangeInfo.primitiveOffset = 0;
    buildRangeInfo.firstVertex = 0;
    buildRangeInfo.transformOffset = 0;
    
    const VkAccelerationStructureBuildRangeInfoKHR* pBuildRangeInfo = &buildRangeInfo;
    
    // Submit synchronous build
    if (m_device->buildAccelerationStructuresKHR(m_device->device(), VK_NULL_HANDLE, 1, &buildInfoCopy, &pBuildRangeInfo) != VK_SUCCESS) {
        std::cerr << "Failed to build TLAS\n";
        return false;
    }
    
    std::cout << "✓ TLAS built\n";
    return true;
}

} // namespace vox
