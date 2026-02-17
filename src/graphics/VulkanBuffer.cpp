#include "vox/graphics/VulkanBuffer.h"
#include "vox/graphics/VulkanDevice.h"
#include <cstring>
#include <stdexcept>

namespace vox {

VulkanBuffer::VulkanBuffer(VulkanDevice* device,
                           VkDeviceSize size,
                           VkBufferUsageFlags usage,
                           VkMemoryPropertyFlags properties)
    : m_device(device), m_size(size) {
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device->device(), &bufferInfo, nullptr, &m_buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer");
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device->device(), m_buffer, &memReqs);

    VkMemoryAllocateFlagsInfo allocFlags{};
    allocFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = device->findMemoryType(memReqs.memoryTypeBits, properties);
    allocInfo.pNext = &allocFlags;

    if (vkAllocateMemory(device->device(), &allocInfo, nullptr, &m_memory) != VK_SUCCESS) {
        vkDestroyBuffer(device->device(), m_buffer, nullptr);
        throw std::runtime_error("Failed to allocate buffer memory");
    }

    vkBindBufferMemory(device->device(), m_buffer, m_memory, 0);
    
    queryDeviceAddress();
}

VulkanBuffer::~VulkanBuffer() {
    if (m_mappedData) vkUnmapMemory(m_device->device(), m_memory);
    if (m_memory != VK_NULL_HANDLE) vkFreeMemory(m_device->device(), m_memory, nullptr);
    if (m_buffer != VK_NULL_HANDLE) vkDestroyBuffer(m_device->device(), m_buffer, nullptr);
}

void* VulkanBuffer::map() {
    if (m_mappedData) return m_mappedData;
    vkMapMemory(m_device->device(), m_memory, 0, m_size, 0, &m_mappedData);
    return m_mappedData;
}

void VulkanBuffer::unmap() {
    if (m_mappedData) {
        vkUnmapMemory(m_device->device(), m_memory);
        m_mappedData = nullptr;
    }
}

void VulkanBuffer::copyData(const void* data, VkDeviceSize size) {
    if (size > m_size) throw std::runtime_error("Data size exceeds buffer capacity");
    void* mapped = map();
    memcpy(mapped, data, size);
}

void VulkanBuffer::queryDeviceAddress() {
    VkBufferDeviceAddressInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = m_buffer;
    m_deviceAddress = m_device->getBufferDeviceAddressKHR(m_device->device(), &info);
}

} // namespace vox
