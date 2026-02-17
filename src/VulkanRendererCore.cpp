#include "vox/VulkanRenderer.h"
#include "vox/SparseVoxelOctree.h"

namespace vox {

VulkanRenderer::VulkanRenderer(SDL_Window* window) : m_window(window) {}

VulkanRenderer::~VulkanRenderer() {
    if (!m_initialized) return;

    vkDeviceWaitIdle(m_device);

    // Compute shader resources
    if (m_rtPipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_rtPipeline, nullptr);
    if (m_rtPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_device, m_rtPipelineLayout, nullptr);
    if (m_rtDescPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(m_device, m_rtDescPool, nullptr);
    if (m_rtDescSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(m_device, m_rtDescSetLayout, nullptr);

    if (m_rtImageView != VK_NULL_HANDLE) vkDestroyImageView(m_device, m_rtImageView, nullptr);
    if (m_rtImage != VK_NULL_HANDLE) vkDestroyImage(m_device, m_rtImage, nullptr);
    if (m_rtImageMemory != VK_NULL_HANDLE) vkFreeMemory(m_device, m_rtImageMemory, nullptr);

    // Octree buffers
    if (m_octreeNodesBuffer != VK_NULL_HANDLE) vkDestroyBuffer(m_device, m_octreeNodesBuffer, nullptr);
    if (m_octreeNodesMemory != VK_NULL_HANDLE) vkFreeMemory(m_device, m_octreeNodesMemory, nullptr);
    if (m_octreeColorsBuffer != VK_NULL_HANDLE) vkDestroyBuffer(m_device, m_octreeColorsBuffer, nullptr);
    if (m_octreeColorsMemory != VK_NULL_HANDLE) vkFreeMemory(m_device, m_octreeColorsMemory, nullptr);

    // RTX resources
    if (m_useRTX) {
        if (m_sbtBuffer != VK_NULL_HANDLE) vkDestroyBuffer(m_device, m_sbtBuffer, nullptr);
        if (m_sbtMemory != VK_NULL_HANDLE) vkFreeMemory(m_device, m_sbtMemory, nullptr);
        if (m_aabbBuffer != VK_NULL_HANDLE) vkDestroyBuffer(m_device, m_aabbBuffer, nullptr);
        if (m_aabbMemory != VK_NULL_HANDLE) vkFreeMemory(m_device, m_aabbMemory, nullptr);
        if (m_tlas != VK_NULL_HANDLE) vkDestroyAccelerationStructureKHR(m_device, m_tlas, nullptr);
        if (m_tlasBuffer != VK_NULL_HANDLE) vkDestroyBuffer(m_device, m_tlasBuffer, nullptr);
        if (m_tlasMemory != VK_NULL_HANDLE) vkFreeMemory(m_device, m_tlasMemory, nullptr);
        if (m_blas != VK_NULL_HANDLE) vkDestroyAccelerationStructureKHR(m_device, m_blas, nullptr);
        if (m_blasBuffer != VK_NULL_HANDLE) vkDestroyBuffer(m_device, m_blasBuffer, nullptr);
        if (m_blasMemory != VK_NULL_HANDLE) vkFreeMemory(m_device, m_blasMemory, nullptr);
    }

    if (m_imgAvail != VK_NULL_HANDLE) vkDestroySemaphore(m_device, m_imgAvail, nullptr);
    if (m_renderDone != VK_NULL_HANDLE) vkDestroySemaphore(m_device, m_renderDone, nullptr);
    if (m_frameTimeline != VK_NULL_HANDLE) vkDestroySemaphore(m_device, m_frameTimeline, nullptr);
    if (m_cmdPool != VK_NULL_HANDLE) vkDestroyCommandPool(m_device, m_cmdPool, nullptr);
    for (auto iv : m_imageViews) vkDestroyImageView(m_device, iv, nullptr);

    if (m_swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
    if (m_device != VK_NULL_HANDLE) vkDestroyDevice(m_device, nullptr);
    if (m_surface != VK_NULL_HANDLE) vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    if (m_instance != VK_NULL_HANDLE) vkDestroyInstance(m_instance, nullptr);
}

} // namespace vox
