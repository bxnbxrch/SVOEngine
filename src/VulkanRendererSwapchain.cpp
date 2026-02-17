#include "vox/VulkanRenderer.h"
#include "VulkanRendererCommon.h"
#include <SDL.h>
#include <algorithm>
#include <iostream>
#include <vector>

namespace vox {

void VulkanRenderer::recreateSwapchain() {
    if (!m_initialized) return;

    std::cout << "Recreating swapchain..." << std::endl;

    // Wait for device to be idle
    vkDeviceWaitIdle(m_device);

    // Clean up old resources
    for (auto iv : m_imageViews) vkDestroyImageView(m_device, iv, nullptr);
    m_imageViews.clear();

    if (m_rtImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_rtImageView, nullptr);
        m_rtImageView = VK_NULL_HANDLE;
    }

    if (m_rtImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, m_rtImage, nullptr);
        m_rtImage = VK_NULL_HANDLE;
    }

    if (m_rtImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_rtImageMemory, nullptr);
        m_rtImageMemory = VK_NULL_HANDLE;
    }

    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }

    // Get new window size
    int w, h;
    SDL_GetWindowSize(m_window, &w, &h);
    m_extent.width = w;
    m_extent.height = h;

    // Query surface capabilities again
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &caps);

    if (m_extent.width == 0 || m_extent.height == 0) {
        m_extent = caps.currentExtent;
    } else {
        m_extent.width = std::max(caps.minImageExtent.width, std::min(caps.maxImageExtent.width, m_extent.width));
        m_extent.height = std::max(caps.minImageExtent.height, std::min(caps.maxImageExtent.height, m_extent.height));
    }

    // Recreate swapchain
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    {
        uint32_t pmc = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &pmc, nullptr);
        std::vector<VkPresentModeKHR> pms(pmc);
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &pmc, pms.data());
        for (auto &p : pms) if (p == VK_PRESENT_MODE_MAILBOX_KHR) { presentMode = p; break; }
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = m_surface;
    sci.minImageCount = imageCount;
    sci.imageFormat = m_surfaceFormat;
    sci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    sci.imageExtent = m_extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = presentMode;
    sci.clipped = VK_TRUE;
    sci.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(m_device, &sci, nullptr, &m_swapchain) != VK_SUCCESS) {
        std::cerr << "Failed to recreate swapchain" << std::endl;
        return;
    }

    // Get swapchain images
    uint32_t actualCount = 0;
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &actualCount, nullptr);
    m_swapImages.resize(actualCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &actualCount, m_swapImages.data());

    // Recreate image views
    m_imageViews.resize(actualCount);
    for (uint32_t i = 0; i < actualCount; ++i) {
        VkImageViewCreateInfo ivci{};
        ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image = m_swapImages[i];
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = m_surfaceFormat;
        ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.baseMipLevel = 0;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.baseArrayLayer = 0;
        ivci.subresourceRange.layerCount = 1;
        vkCreateImageView(m_device, &ivci, nullptr, &m_imageViews[i]);
    }

    // Recreate command buffers to match swapchain image count
    if (!m_cmdBuffers.empty()) {
        vkFreeCommandBuffers(m_device, m_cmdPool, static_cast<uint32_t>(m_cmdBuffers.size()), m_cmdBuffers.data());
    }
    m_cmdBuffers.resize(m_swapImages.size());
    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = m_cmdPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = static_cast<uint32_t>(m_cmdBuffers.size());
    vkAllocateCommandBuffers(m_device, &cbai, m_cmdBuffers.data());
    m_cmdBufferValues.assign(m_cmdBuffers.size(), 0);

    // Recreate RT storage image
    {
        VkImageCreateInfo ici{};
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = VK_FORMAT_B8G8R8A8_SRGB;
        ici.extent = {m_extent.width, m_extent.height, 1};
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        vkCreateImage(m_device, &ici, nullptr, &m_rtImage);

        VkMemoryRequirements memReq{};
        vkGetImageMemoryRequirements(m_device, m_rtImage, &memReq);

        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);
        auto findMemoryType = [&](uint32_t typeFilter, VkMemoryPropertyFlags props) -> uint32_t {
            for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
                if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props) return i;
            }
            return 0;
        };

        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = memReq.size;
        mai.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        vkAllocateMemory(m_device, &mai, nullptr, &m_rtImageMemory);
        vkBindImageMemory(m_device, m_rtImage, m_rtImageMemory, 0);

        VkImageViewCreateInfo ivci{};
        ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image = m_rtImage;
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = VK_FORMAT_B8G8R8A8_SRGB;
        ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.baseMipLevel = 0;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.baseArrayLayer = 0;
        ivci.subresourceRange.layerCount = 1;

        vkCreateImageView(m_device, &ivci, nullptr, &m_rtImageView);
    }

    // Transition RT image to GENERAL layout
    {
        VkCommandBufferAllocateInfo cbai{};
        cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool = m_cmdPool;
        cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;

        VkCommandBuffer transCmd;
        vkAllocateCommandBuffers(m_device, &cbai, &transCmd);

        VkCommandBufferBeginInfo cbbi{};
        cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(transCmd, &cbbi);

        VkImageMemoryBarrier2 imb{};
        imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        imb.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        imb.srcAccessMask = 0;
        imb.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        imb.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        imb.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imb.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        imb.image = m_rtImage;
        imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imb.subresourceRange.baseMipLevel = 0;
        imb.subresourceRange.levelCount = 1;
        imb.subresourceRange.baseArrayLayer = 0;
        imb.subresourceRange.layerCount = 1;

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &imb;

        vkCmdPipelineBarrier2(transCmd, &depInfo);

        vkEndCommandBuffer(transCmd);

        VkCommandBufferSubmitInfo cmdSubmit{};
        cmdSubmit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cmdSubmit.commandBuffer = transCmd;

        VkSubmitInfo2 si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        si.commandBufferInfoCount = 1;
        si.pCommandBufferInfos = &cmdSubmit;
        vkQueueSubmit2(m_graphicsQueue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_graphicsQueue);

        vkFreeCommandBuffers(m_device, m_cmdPool, 1, &transCmd);
    }

    // Update descriptor set with new RT image view
    {
        VkWriteDescriptorSet write{};
        VkDescriptorImageInfo imgInfo{};
        imgInfo.imageView = m_rtImageView;
        imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_rtDescSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.pImageInfo = &imgInfo;

        vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    }

    std::cout << "Swapchain recreated: " << m_extent.width << "x" << m_extent.height << std::endl;
}

} // namespace vox
