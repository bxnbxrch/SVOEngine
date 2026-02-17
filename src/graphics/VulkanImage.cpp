#include "vox/graphics/VulkanImage.h"
#include "vox/graphics/VulkanDevice.h"
#include <stdexcept>

namespace vox {

VulkanImage::VulkanImage(VulkanDevice* device,
                         VkExtent2D extent,
                         VkFormat format,
                         VkImageUsageFlags usage)
    : m_device(device), m_format(format), m_extent(extent) {
    
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {extent.width, extent.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    if (vkCreateImage(device->device(), &imageInfo, nullptr, &m_image) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image");
    }

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device->device(), m_image, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = device->findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device->device(), &allocInfo, nullptr, &m_memory) != VK_SUCCESS) {
        vkDestroyImage(device->device(), m_image, nullptr);
        throw std::runtime_error("Failed to allocate image memory");
    }

    vkBindImageMemory(device->device(), m_image, m_memory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device->device(), &viewInfo, nullptr, &m_imageView) != VK_SUCCESS) {
        vkDestroyImage(device->device(), m_image, nullptr);
        vkFreeMemory(device->device(), m_memory, nullptr);
        throw std::runtime_error("Failed to create image view");
    }
}

VulkanImage::~VulkanImage() {
    if (m_imageView != VK_NULL_HANDLE) vkDestroyImageView(m_device->device(), m_imageView, nullptr);
    if (m_memory != VK_NULL_HANDLE) vkFreeMemory(m_device->device(), m_memory, nullptr);
    if (m_image != VK_NULL_HANDLE) vkDestroyImage(m_device->device(), m_image, nullptr);
}

} // namespace vox
