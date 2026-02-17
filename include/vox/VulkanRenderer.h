#pragma once

#include <vulkan/vulkan.h>
#include <SDL.h>
#include <vector>
#include <memory>
#include <chrono>

namespace vox {
class SparseVoxelOctree;
}

namespace vox {

class VulkanRenderer {
public:
    explicit VulkanRenderer(SDL_Window* window);
    ~VulkanRenderer();

    bool init();
    void drawFrame();
    void toggleGridOverlay();

    // input-driven controls
    void adjustOrbitRadius(float delta);
    void adjustCamHeight(float delta);
    void togglePauseOrbit();
    void adjustOrbitAngle(float deltaRadians);

    bool valid() const { return m_initialized; }

private:
    SDL_Window* m_window = nullptr;
    bool m_initialized = false;

    // Vulkan objects
    VkInstance m_instance = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    uint32_t m_graphicsQueueFamily = UINT32_MAX;

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> m_swapImages;
    std::vector<VkImageView> m_imageViews;
    VkFormat m_surfaceFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_extent{};

    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_framebuffers;

    VkCommandPool m_cmdPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_cmdBuffers;

    VkSemaphore m_imgAvail = VK_NULL_HANDLE;
    VkSemaphore m_renderDone = VK_NULL_HANDLE;

    // Compute shader ray tracing (fallback for non-RTX hardware)
    std::unique_ptr<SparseVoxelOctree> m_octree;

    VkImage m_rtImage = VK_NULL_HANDLE; // storage image for raytrace output
    VkDeviceMemory m_rtImageMemory = VK_NULL_HANDLE;
    VkImageView m_rtImageView = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_rtDescSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_rtDescPool = VK_NULL_HANDLE;
    VkDescriptorSet m_rtDescSet = VK_NULL_HANDLE;

    VkPipelineLayout m_rtPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_rtPipeline = VK_NULL_HANDLE;

    // runtime debug flags
    bool m_showSvoOverlay = true; // default: visible

    // camera / input-controlled parameters
    float m_orbitRadius = 400.0f;
    float m_camHeight = 120.0f;
    float m_fov = 45.0f;
    bool m_pauseOrbit = false;
    float m_pausedTime = 0.0f;
    std::chrono::high_resolution_clock::time_point m_startTime;

    // manual orbit (mouse drag)
    float m_orbitAngle = 0.0f; // radians
    bool m_manualOrbit = false;

    // Octree data buffers
    VkBuffer m_octreeNodesBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_octreeNodesMemory = VK_NULL_HANDLE;
    VkBuffer m_octreeColorsBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_octreeColorsMemory = VK_NULL_HANDLE;
};

} // namespace vox
