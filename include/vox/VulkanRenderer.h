#pragma once

#include <vulkan/vulkan.h>
#include <SDL.h>
#include <vector>
#include <memory>
#include <chrono>
#include <glm/glm.hpp>

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
    void toggleDebugLighting();
    void toggleGUI();
    void recreateSwapchain();

    // input-driven controls
    void adjustDistance(float delta);
    void adjustPitch(float delta);
    void adjustYaw(float delta);
    void togglePauseOrbit();
    
    // Free-fly camera controls
    void toggleCameraMode();
    void moveCameraForward(float amount);
    void moveCameraRight(float amount);
    void moveCameraUp(float amount);
    void rotateCameraYaw(float delta);
    void rotateCameraPitch(float delta);
    // set free-fly camera position and forward direction (for benchmarking)
    void setCameraTransform(const glm::vec3 &pos, const glm::vec3 &forward);
    
    // check if in free-fly camera mode
    bool isFreeFlyMode() const { return m_freeFlyCameraMode; }
    
    // check if GUI is visible
    bool isGUIVisible() const { return m_guiVisible; }

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

    uint32_t m_gridSize = 256;

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

    VkImage m_postImage = VK_NULL_HANDLE; // postprocess output (bloom)
    VkDeviceMemory m_postImageMemory = VK_NULL_HANDLE;
    VkImageView m_postImageView = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_rtDescSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_rtDescPool = VK_NULL_HANDLE;
    VkDescriptorSet m_rtDescSet = VK_NULL_HANDLE;

    VkPipelineLayout m_rtPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_rtPipeline = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_postDescSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_postDescPool = VK_NULL_HANDLE;
    VkDescriptorSet m_postDescSet = VK_NULL_HANDLE;
    VkPipelineLayout m_postPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_postPipeline = VK_NULL_HANDLE;

    VkDescriptorPool m_imguiPool = VK_NULL_HANDLE;
    bool m_imguiInitialized = false;
    bool m_guiVisible = true;

    // runtime debug flags
    bool m_showSvoOverlay = false; // default: disabled for performance
    int m_debugMode = 0;
    bool m_bloomEnabled = false;
    float m_resolutionScale = 1.0f; // render scale (0.5 = half res)
    bool m_temporalEnabled = false; // temporal reprojection (not yet implemented)
    float m_bloomThreshold = 0.9f;
    float m_bloomIntensity = 0.6f;
    float m_bloomRadius = 2.0f;

    // camera / input-controlled parameters
    float m_distance = 400.0f;      // distance from target
    float m_yaw = 0.0f;             // horizontal angle (radians)
    float m_pitch = 0.4f;           // vertical angle (radians), 0 = horizon, PI/2 = top
    float m_fov = 45.0f;
    bool m_pauseOrbit = false;
    float m_pausedTime = 0.0f;
    std::chrono::high_resolution_clock::time_point m_startTime;
    bool m_manualControl = false;
    
    // Free-fly camera mode
    bool m_freeFlyCameraMode = false;
    glm::vec3 m_cameraPosition{1024.0f, 585.0f, 1024.0f}; // Start at center +Y
    glm::vec3 m_cameraForward{1.0f, 0.0f, 0.0f};
    glm::vec3 m_cameraRight{0.0f, 0.0f, -1.0f};
    glm::vec3 m_cameraUp{0.0f, 1.0f, 0.0f};
    float m_freeFlyYaw = 0.0f;
    float m_freeFlyPitch = 0.0f;

    // Octree data buffers
    VkBuffer m_octreeNodesBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_octreeNodesMemory = VK_NULL_HANDLE;
    VkBuffer m_octreeColorsBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_octreeColorsMemory = VK_NULL_HANDLE;

    VkBuffer m_emissiveBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_emissiveMemory = VK_NULL_HANDLE;

    VkBuffer m_spatialGridBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_spatialGridMemory = VK_NULL_HANDLE;

    VkBuffer m_shaderParamsBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_shaderParamsMemory = VK_NULL_HANDLE;

    struct ShaderParamsCPU {
        glm::vec4 bgColor;
        glm::vec4 keyDir;
        glm::vec4 fillDir;
        glm::vec4 params0; // ambient, emissiveSelf, emissiveDirect, attenFactor
        glm::vec4 params1; // attenBias, maxLights, debugMode, ddaEps
        glm::vec4 params2; // ddaEpsScale, reserved, reserved, reserved
    } m_shaderParams{
        glm::vec4(0.05f, 0.05f, 0.08f, 0.0f),
        glm::vec4(glm::normalize(glm::vec3(0.6f, 0.8f, 0.4f)), 0.6f),
        glm::vec4(glm::normalize(glm::vec3(-0.3f, -0.5f, -0.2f)), 0.2f),
        glm::vec4(0.3f, 4.0f, 6.0f, 0.02f),
        glm::vec4(1.0f, 0.0f, 0.0f, 0.001f),
        glm::vec4(0.0002f, 0.0f, 0.0f, 0.0f)
    };
    
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
