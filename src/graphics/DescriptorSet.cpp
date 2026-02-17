#include "vox/graphics/DescriptorSet.h"
#include "vox/graphics/VulkanDevice.h"

namespace vox {

DescriptorSet::DescriptorSet(VulkanDevice* device) : m_device(device) {}

DescriptorSet::~DescriptorSet() {
    if (m_set != VK_NULL_HANDLE && m_pool != VK_NULL_HANDLE) {
        // Descriptors are freed with pool
    }
    if (m_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device->device(), m_pool, nullptr);
    }
    if (m_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device->device(), m_layout, nullptr);
    }
}

bool DescriptorSet::init(const std::vector<VkDescriptorSetLayoutBinding>& bindings) {
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = bindings.size();
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(m_device->device(), &layoutInfo, nullptr, &m_layout) != VK_SUCCESS) {
        return false;
    }

    std::vector<VkDescriptorPoolSize> poolSizes;
    for (const auto& binding : bindings) {
        VkDescriptorPoolSize size{};
        size.type = binding.descriptorType;
        size.descriptorCount = binding.descriptorCount;
        poolSizes.push_back(size);
    }

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = poolSizes.size();
    poolInfo.pPoolSizes = poolSizes.data();

    if (vkCreateDescriptorPool(m_device->device(), &poolInfo, nullptr, &m_pool) != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(m_device->device(), m_layout, nullptr);
        return false;
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_layout;

    if (vkAllocateDescriptorSets(m_device->device(), &allocInfo, &m_set) != VK_SUCCESS) {
        vkDestroyDescriptorPool(m_device->device(), m_pool, nullptr);
        vkDestroyDescriptorSetLayout(m_device->device(), m_layout, nullptr);
        return false;
    }

    return true;
}

void DescriptorSet::writeBuffer(uint32_t binding, VkBuffer buffer) {
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = buffer;
    bufferInfo.offset = 0;
    bufferInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_set;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(m_device->device(), 1, &write, 0, nullptr);
}

void DescriptorSet::writeImage(uint32_t binding, VkImageView imageView, VkImageLayout layout) {
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageView = imageView;
    imageInfo.imageLayout = layout;
    imageInfo.sampler = VK_NULL_HANDLE;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_set;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(m_device->device(), 1, &write, 0, nullptr);
}

void DescriptorSet::writeAccelerationStructure(uint32_t binding, VkAccelerationStructureKHR accelStruct) {
    VkWriteDescriptorSetAccelerationStructureKHR accelWrite{};
    accelWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    accelWrite.accelerationStructureCount = 1;
    accelWrite.pAccelerationStructures = &accelStruct;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_set;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    write.pNext = &accelWrite;

    vkUpdateDescriptorSets(m_device->device(), 1, &write, 0, nullptr);
}

} // namespace vox
