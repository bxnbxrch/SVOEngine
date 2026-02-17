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
    void recreateSwapchain();

    // input-driven controls
    void adjustDistance(float delta);
    void adjustPitch(float delta);
    void adjustYaw(float delta);
    void togglePauseOrbit();

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

    VkCommandPool m_cmdPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_cmdBuffers;

    VkSemaphore m_imgAvail = VK_NULL_HANDLE;
    VkSemaphore m_renderDone = VK_NULL_HANDLE;
    VkSemaphore m_frameTimeline = VK_NULL_HANDLE;
    uint64_t m_frameValue = 0;
    std::vector<uint64_t> m_cmdBufferValues;

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
    float m_distance = 400.0f;      // distance from target
    float m_yaw = 0.0f;             // horizontal angle (radians)
    float m_pitch = 0.4f;           // vertical angle (radians), 0 = horizon, PI/2 = top
    float m_fov = 45.0f;
    bool m_pauseOrbit = false;
    float m_pausedTime = 0.0f;
    std::chrono::high_resolution_clock::time_point m_startTime;
    bool m_manualControl = false;

    // Octree data buffers
    VkBuffer m_octreeNodesBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_octreeNodesMemory = VK_NULL_HANDLE;
    VkBuffer m_octreeColorsBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_octreeColorsMemory = VK_NULL_HANDLE;
    
    // RTX ray tracing
    bool m_useRTX = false;
    
    // Acceleration structures
    VkAccelerationStructureKHR m_blas = VK_NULL_HANDLE;
    VkAccelerationStructureKHR m_tlas = VK_NULL_HANDLE;
    VkBuffer m_blasBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_blasMemory = VK_NULL_HANDLE;
    VkBuffer m_tlasBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_tlasMemory = VK_NULL_HANDLE;
    
    // AABB buffer for voxels  
    VkBuffer m_aabbBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_aabbMemory = VK_NULL_HANDLE;
    
    // Shader Binding Table
    VkBuffer m_sbtBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_sbtMemory = VK_NULL_HANDLE;
    VkStridedDeviceAddressRegionKHR m_rgenRegion{};
    VkStridedDeviceAddressRegionKHR m_missRegion{};
    VkStridedDeviceAddressRegionKHR m_hitRegion{};
    VkStridedDeviceAddressRegionKHR m_callRegion{};
    
    // Ray tracing properties
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_rtPipelineProperties{};
    
    // Function pointers
    PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR = nullptr;
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR = nullptr;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR = nullptr;
    PFN_vkBuildAccelerationStructuresKHR vkBuildAccelerationStructuresKHR = nullptr;
    PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR = nullptr;
    PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR = nullptr;
    PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR = nullptr;
};

} // namespace vox
