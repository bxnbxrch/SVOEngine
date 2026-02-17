#include "vox/VulkanRenderer.h"
#include "vox/SparseVoxelOctree.h"
#include "vox/Shader.h"
#include "VulkanRendererCommon.h"
#include <SDL_vulkan.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace vox {

static bool hasExtension(const std::vector<VkExtensionProperties>& exts, const char* name) {
    for (auto &e : exts) if (std::string(e.extensionName) == name) return true;
    return false;
}

bool VulkanRenderer::init() {
    if (!m_window) {
        std::cerr << "VulkanRenderer: no SDL_Window provided" << std::endl;
        return false;
    }

    // Instance extensions required by SDL
    unsigned int extCount = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(m_window, &extCount, nullptr)) {
        std::cerr << "SDL_Vulkan_GetInstanceExtensions failed\n";
        return false;
    }
    std::vector<const char*> extensions(extCount);
    if (!SDL_Vulkan_GetInstanceExtensions(m_window, &extCount, extensions.data())) {
        std::cerr << "SDL_Vulkan_GetInstanceExtensions failed (2)\n";
        return false;
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "vox";
    appInfo.applicationVersion = VK_MAKE_VERSION(0,1,0);
    appInfo.pEngineName = "no-engine";
    appInfo.engineVersion = VK_MAKE_VERSION(0,1,0);
    appInfo.apiVersion = VK_API_VERSION_1_4;

    VkInstanceCreateInfo icci{};
    icci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    icci.pApplicationInfo = &appInfo;
    icci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    icci.ppEnabledExtensionNames = extensions.data();

    if (vkCreateInstance(&icci, nullptr, &m_instance) != VK_SUCCESS) {
        std::cerr << "vkCreateInstance failed" << std::endl;
        return false;
    }

    if (!SDL_Vulkan_CreateSurface(m_window, m_instance, &m_surface)) {
        std::cerr << "SDL_Vulkan_CreateSurface failed" << std::endl;
        vkDestroyInstance(m_instance, nullptr);
        return false;
    }

    // pick physical device with graphics+present
    uint32_t gpuCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &gpuCount, nullptr);
    if (gpuCount == 0) {
        std::cerr << "No Vulkan physical devices found\n";
        return false;
    }
    std::vector<VkPhysicalDevice> phys(gpuCount);
    vkEnumeratePhysicalDevices(m_instance, &gpuCount, phys.data());

    for (auto dev : phys) {
        uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, nullptr);
        std::vector<VkQueueFamilyProperties> qprops(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, qprops.data());
        for (uint32_t i = 0; i < qCount; ++i) {
            VkBool32 presentSupported = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, m_surface, &presentSupported);
            if ((qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupported) {
                m_physicalDevice = dev;
                m_graphicsQueueFamily = i;
                break;
            }
        }
        if (m_physicalDevice != VK_NULL_HANDLE) break;
    }

    if (m_physicalDevice == VK_NULL_HANDLE) {
        std::cerr << "No suitable GPU (graphics+present)\n";
        return false;
    }

    // check for ray tracing extensions
    uint32_t devExtCount = 0;
    vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &devExtCount, nullptr);
    std::vector<VkExtensionProperties> devExts(devExtCount);
    vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &devExtCount, devExts.data());

    bool hasSwap = hasExtension(devExts, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    bool hasRayTracingPipeline = hasExtension(devExts, "VK_KHR_ray_tracing_pipeline");
    bool hasAccelStruct = hasExtension(devExts, "VK_KHR_acceleration_structure");
    bool hasDeferredHost = hasExtension(devExts, "VK_KHR_deferred_host_operations");
    bool hasBufferDevAddr = hasExtension(devExts, "VK_KHR_buffer_device_address");

    if (!hasSwap) {
        std::cerr << "GPU lacks VK_KHR_swapchain" << std::endl;
        return false;
    }

    m_useRTX = hasRayTracingPipeline && hasAccelStruct && hasDeferredHost && hasBufferDevAddr;

    if (m_useRTX) {
        std::cout << "Hardware RTX ray tracing enabled!" << std::endl;
    } else {
        std::cerr << "Hardware RTX not available. Missing extensions." << std::endl;
        return false;
    }

    float qprio = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = m_graphicsQueueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &qprio;

    std::vector<const char*> devExtsReq = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    if (m_useRTX) {
        devExtsReq.push_back("VK_KHR_ray_tracing_pipeline");
        devExtsReq.push_back("VK_KHR_acceleration_structure");
        devExtsReq.push_back("VK_KHR_deferred_host_operations");
        devExtsReq.push_back("VK_KHR_buffer_device_address");
        devExtsReq.push_back("VK_KHR_spirv_1_4");
        devExtsReq.push_back("VK_KHR_shader_float_controls");
    }

    // Enable Vulkan 1.2/1.3 features
    VkPhysicalDeviceVulkan12Features vulkan12Features{};
    vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vulkan12Features.timelineSemaphore = VK_TRUE;

    VkPhysicalDeviceVulkan13Features vulkan13Features{};
    vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vulkan13Features.dynamicRendering = VK_TRUE;
    vulkan13Features.synchronization2 = VK_TRUE;
    vulkan13Features.pNext = &vulkan12Features;

    // Enable ray tracing features
    VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures{};
    bufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    bufferDeviceAddressFeatures.bufferDeviceAddress = VK_TRUE;
    bufferDeviceAddressFeatures.pNext = &vulkan13Features;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures{};
    rayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    rayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;
    rayTracingPipelineFeatures.pNext = &bufferDeviceAddressFeatures;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelStructFeatures{};
    accelStructFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    accelStructFeatures.accelerationStructure = VK_TRUE;
    accelStructFeatures.pNext = &rayTracingPipelineFeatures;

    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = static_cast<uint32_t>(devExtsReq.size());
    dci.ppEnabledExtensionNames = devExtsReq.data();
    if (m_useRTX) {
        dci.pNext = &accelStructFeatures;
    } else {
        dci.pNext = &vulkan13Features;
    }

    if (vkCreateDevice(m_physicalDevice, &dci, nullptr, &m_device) != VK_SUCCESS) {
        std::cerr << "vkCreateDevice failed" << std::endl;
        return false;
    }
    vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);

    // Load ray tracing function pointers
    if (m_useRTX) {
        vkGetBufferDeviceAddressKHR = (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(m_device, "vkGetBufferDeviceAddressKHR");
        vkCreateAccelerationStructureKHR = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(m_device, "vkCreateAccelerationStructureKHR");
        vkDestroyAccelerationStructureKHR = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(m_device, "vkDestroyAccelerationStructureKHR");
        vkGetAccelerationStructureBuildSizesKHR = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(m_device, "vkGetAccelerationStructureBuildSizesKHR");
        vkGetAccelerationStructureDeviceAddressKHR = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(m_device, "vkGetAccelerationStructureDeviceAddressKHR");
        vkCmdBuildAccelerationStructuresKHR = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(m_device, "vkCmdBuildAccelerationStructuresKHR");
        vkBuildAccelerationStructuresKHR = (PFN_vkBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(m_device, "vkBuildAccelerationStructuresKHR");
        vkCreateRayTracingPipelinesKHR = (PFN_vkCreateRayTracingPipelinesKHR)vkGetDeviceProcAddr(m_device, "vkCreateRayTracingPipelinesKHR");
        vkGetRayTracingShaderGroupHandlesKHR = (PFN_vkGetRayTracingShaderGroupHandlesKHR)vkGetDeviceProcAddr(m_device, "vkGetRayTracingShaderGroupHandlesKHR");
        vkCmdTraceRaysKHR = (PFN_vkCmdTraceRaysKHR)vkGetDeviceProcAddr(m_device, "vkCmdTraceRaysKHR");

        // Get ray tracing properties (requires Vulkan 1.1+)
        PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2 =
            (PFN_vkGetPhysicalDeviceProperties2)vkGetInstanceProcAddr(m_instance, "vkGetPhysicalDeviceProperties2");

        if (vkGetPhysicalDeviceProperties2) {
            m_rtPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
            VkPhysicalDeviceProperties2 props2{};
            props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            props2.pNext = &m_rtPipelineProperties;
            vkGetPhysicalDeviceProperties2(m_physicalDevice, &props2);

            std::cout << "RTX properties loaded. Max recursion depth: " << m_rtPipelineProperties.maxRayRecursionDepth << std::endl;
        }
    }

    // surface format
    VkSurfaceFormatKHR sf{};
    {
        uint32_t count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &count, nullptr);
        std::vector<VkSurfaceFormatKHR> fmts(count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &count, fmts.data());
        if (count == 1 && fmts[0].format == VK_FORMAT_UNDEFINED) {
            sf.format = VK_FORMAT_B8G8R8A8_SRGB;
            sf.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        } else {
            sf = fmts[0];
        }
    }
    m_surfaceFormat = sf.format;

    // capabilities / extent
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &caps);
    if (caps.currentExtent.width != UINT32_MAX) m_extent = caps.currentExtent;
    else {
        int w, h; SDL_GetWindowSize(m_window, &w, &h);
        m_extent.width = static_cast<uint32_t>(w);
        m_extent.height = static_cast<uint32_t>(h);
        m_extent.width = std::max(caps.minImageExtent.width, std::min(caps.maxImageExtent.width, m_extent.width));
        m_extent.height = std::max(caps.minImageExtent.height, std::min(caps.maxImageExtent.height, m_extent.height));
    }

    // present mode
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
    sci.imageColorSpace = sf.colorSpace;
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
        std::cerr << "vkCreateSwapchainKHR failed" << std::endl;
        return false;
    }

    uint32_t actualCount = 0;
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &actualCount, nullptr);
    m_swapImages.resize(actualCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &actualCount, m_swapImages.data());

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

    // command pool + buffers
    VkCommandPoolCreateInfo pc{};
    pc.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pc.queueFamilyIndex = m_graphicsQueueFamily;
    pc.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCreateCommandPool(m_device, &pc, nullptr, &m_cmdPool);

    m_cmdBuffers.resize(m_swapImages.size());
    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = m_cmdPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = static_cast<uint32_t>(m_cmdBuffers.size());
    vkAllocateCommandBuffers(m_device, &cbai, m_cmdBuffers.data());

    VkSemaphoreCreateInfo semci{};
    semci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    vkCreateSemaphore(m_device, &semci, nullptr, &m_imgAvail);
    vkCreateSemaphore(m_device, &semci, nullptr, &m_renderDone);

    VkSemaphoreTypeCreateInfo timelineInfo{};
    timelineInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineInfo.initialValue = 0;

    semci.pNext = &timelineInfo;
    vkCreateSemaphore(m_device, &semci, nullptr, &m_frameTimeline);
    semci.pNext = nullptr;

    m_cmdBufferValues.assign(m_cmdBuffers.size(), 0);

    // Initialize octree
    m_octree = std::make_unique<SparseVoxelOctree>(8);
    if (!m_octree->loadFromVoxFile("../monu1.vox")) {
        std::cerr << "Failed to load monu1.vox, using test scene instead" << std::endl;
        m_octree->generateTestScene();
    }
    DBGPRINT << "Octree initialized with test scene\n";
    DBGPRINT << "  Nodes: " << m_octree->getNodes().size() << "\n";
    DBGPRINT << "  Colors: " << m_octree->getColors().size() << "\n";

    // === Setup compute shader ray tracing ===

    // 1. Create storage image (will hold raytrace output)
    {
        VkImageCreateInfo ici{};
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = VK_FORMAT_B8G8R8A8_SRGB;  // Match swapchain format (BGRA SRGB)
        ici.extent = {m_extent.width, m_extent.height, 1};
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (vkCreateImage(m_device, &ici, nullptr, &m_rtImage) != VK_SUCCESS) {
            std::cerr << "vkCreateImage (storage) failed\n";
            return false;
        }

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

        if (vkAllocateMemory(m_device, &mai, nullptr, &m_rtImageMemory) != VK_SUCCESS) {
            std::cerr << "vkAllocateMemory (storage image) failed\n";
            return false;
        }

        vkBindImageMemory(m_device, m_rtImage, m_rtImageMemory, 0);

        VkImageViewCreateInfo ivci{};
        ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image = m_rtImage;
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = VK_FORMAT_B8G8R8A8_SRGB;  // Match storage image format
        ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.baseMipLevel = 0;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.baseArrayLayer = 0;
        ivci.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_device, &ivci, nullptr, &m_rtImageView) != VK_SUCCESS) {
            std::cerr << "vkCreateImageView (storage image) failed\n";
            return false;
        }
        DBGPRINT << "Storage image created ("  << m_extent.width << "x" << m_extent.height << ")\n";
    }

    // 2. Create octree GPU buffers
    {
        const auto& nodes = m_octree->getNodes();
        const auto& colors = m_octree->getColors();

        // Node buffer
        VkDeviceSize nodeSize = nodes.size() * sizeof(uint32_t);
        VkBufferCreateInfo bci{};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = nodeSize;
        bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(m_device, &bci, nullptr, &m_octreeNodesBuffer) != VK_SUCCESS) {
            std::cerr << "vkCreateBuffer (octree nodes) failed\n";
            return false;
        }

        VkMemoryRequirements memReq{};
        vkGetBufferMemoryRequirements(m_device, m_octreeNodesBuffer, &memReq);

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
        mai.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(m_device, &mai, nullptr, &m_octreeNodesMemory) != VK_SUCCESS) {
            std::cerr << "vkAllocateMemory (octree nodes) failed\n";
            return false;
        }

        vkBindBufferMemory(m_device, m_octreeNodesBuffer, m_octreeNodesMemory, 0);

        void* dst = nullptr;
        vkMapMemory(m_device, m_octreeNodesMemory, 0, nodeSize, 0, &dst);
        memcpy(dst, nodes.data(), nodeSize);
        vkUnmapMemory(m_device, m_octreeNodesMemory);

        // Color buffer
        VkDeviceSize colorSize = colors.size() * sizeof(uint32_t);
        bci.size = colorSize;

        if (vkCreateBuffer(m_device, &bci, nullptr, &m_octreeColorsBuffer) != VK_SUCCESS) {
            std::cerr << "vkCreateBuffer (octree colors) failed\n";
            return false;
        }

        vkGetBufferMemoryRequirements(m_device, m_octreeColorsBuffer, &memReq);
        mai.allocationSize = memReq.size;
        mai.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(m_device, &mai, nullptr, &m_octreeColorsMemory) != VK_SUCCESS) {
            std::cerr << "vkAllocateMemory (octree colors) failed\n";
            return false;
        }

        vkBindBufferMemory(m_device, m_octreeColorsBuffer, m_octreeColorsMemory, 0);

        dst = nullptr;
        vkMapMemory(m_device, m_octreeColorsMemory, 0, colorSize, 0, &dst);
        memcpy(dst, colors.data(), colorSize);
        vkUnmapMemory(m_device, m_octreeColorsMemory);
        DBGPRINT << "Octree GPU buffers created\n";
    }

    // 2b. Create acceleration structures for RTX (if enabled)
    if (m_useRTX) {
        DBGPRINT << "Creating RTX acceleration structures...\n";

        // Create AABB buffer - single AABB covering entire octree [0,0,0] to [256,256,256]
        struct AABB { float minX, minY, minZ, maxX, maxY, maxZ; };
        AABB aabb = {0.0f, 0.0f, 0.0f, 256.0f, 256.0f, 256.0f};

        VkDeviceSize aabbSize = sizeof(AABB);

        VkBufferCreateInfo bci{};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = aabbSize;
        bci.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

        if (vkCreateBuffer(m_device, &bci, nullptr, &m_aabbBuffer) != VK_SUCCESS) {
            std::cerr << "Failed to create AABB buffer\n";
            return false;
        }

        VkMemoryRequirements memReq{};
        vkGetBufferMemoryRequirements(m_device, m_aabbBuffer, &memReq);

        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);
        auto findMemoryType = [&](uint32_t typeFilter, VkMemoryPropertyFlags props) -> uint32_t {
            for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
                if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props) return i;
            }
            return 0;
        };

        VkMemoryAllocateFlagsInfo allocFlags{};
        allocFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        allocFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = memReq.size;
        mai.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        mai.pNext = &allocFlags;

        if (vkAllocateMemory(m_device, &mai, nullptr, &m_aabbMemory) != VK_SUCCESS) {
            std::cerr << "Failed to allocate AABB memory\n";
            return false;
        }

        vkBindBufferMemory(m_device, m_aabbBuffer, m_aabbMemory, 0);

        void* aabbData = nullptr;
        vkMapMemory(m_device, m_aabbMemory, 0, aabbSize, 0, &aabbData);
        memcpy(aabbData, &aabb, aabbSize);
        vkUnmapMemory(m_device, m_aabbMemory);

        // Get buffer device address
        VkBufferDeviceAddressInfo bdai{};
        bdai.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        bdai.buffer = m_aabbBuffer;
        VkDeviceAddress aabbAddress = vkGetBufferDeviceAddressKHR(m_device, &bdai);

        // Build BLAS (Bottom Level Acceleration Structure)
        VkAccelerationStructureGeometryKHR asGeom{};
        asGeom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        asGeom.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
        asGeom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        asGeom.geometry.aabbs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
        asGeom.geometry.aabbs.data.deviceAddress = aabbAddress;
        asGeom.geometry.aabbs.stride = sizeof(AABB);

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &asGeom;

        uint32_t primitiveCount = 1;
        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
        sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        vkGetAccelerationStructureBuildSizesKHR(m_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                 &buildInfo, &primitiveCount, &sizeInfo);

        // Create BLAS buffer
        bci.size = sizeInfo.accelerationStructureSize;
        bci.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

        if (vkCreateBuffer(m_device, &bci, nullptr, &m_blasBuffer) != VK_SUCCESS) {
            std::cerr << "Failed to create BLAS buffer\n";
            return false;
        }

        vkGetBufferMemoryRequirements(m_device, m_blasBuffer, &memReq);
        mai.allocationSize = memReq.size;
        mai.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(m_device, &mai, nullptr, &m_blasMemory) != VK_SUCCESS) {
            std::cerr << "Failed to allocate BLAS memory\n";
            return false;
        }

        vkBindBufferMemory(m_device, m_blasBuffer, m_blasMemory, 0);

        // Create BLAS
        VkAccelerationStructureCreateInfoKHR asci{};
        asci.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        asci.buffer = m_blasBuffer;
        asci.size = sizeInfo.accelerationStructureSize;
        asci.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

        if (vkCreateAccelerationStructureKHR(m_device, &asci, nullptr, &m_blas) != VK_SUCCESS) {
            std::cerr << "Failed to create BLAS\n";
            return false;
        }

        // Create scratch buffer for building
        VkBuffer scratchBuffer;
        VkDeviceMemory scratchMemory;
        bci.size = sizeInfo.buildScratchSize;
        bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        vkCreateBuffer(m_device, &bci, nullptr, &scratchBuffer);
        vkGetBufferMemoryRequirements(m_device, scratchBuffer, &memReq);
        mai.allocationSize = memReq.size;
        mai.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(m_device, &mai, nullptr, &scratchMemory);
        vkBindBufferMemory(m_device, scratchBuffer, scratchMemory, 0);

        bdai.buffer = scratchBuffer;
        VkDeviceAddress scratchAddress = vkGetBufferDeviceAddressKHR(m_device, &bdai);

        buildInfo.dstAccelerationStructure = m_blas;
        buildInfo.scratchData.deviceAddress = scratchAddress;

        VkAccelerationStructureBuildRangeInfoKHR buildRange{};
        buildRange.primitiveCount = 1;
        buildRange.primitiveOffset = 0;
        buildRange.firstVertex = 0;
        buildRange.transformOffset = 0;

        const VkAccelerationStructureBuildRangeInfoKHR* pBuildRange = &buildRange;

        // Build BLAS
        VkCommandBufferAllocateInfo cmdAllocInfo{};
        cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAllocInfo.commandPool = m_cmdPool;
        cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAllocInfo.commandBufferCount = 1;

        VkCommandBuffer cmdBuf;
        vkAllocateCommandBuffers(m_device, &cmdAllocInfo, &cmdBuf);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(cmdBuf, &beginInfo);
        vkCmdBuildAccelerationStructuresKHR(cmdBuf, 1, &buildInfo, &pBuildRange);
        vkEndCommandBuffer(cmdBuf);

        VkCommandBufferSubmitInfo cmdSubmit{};
        cmdSubmit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cmdSubmit.commandBuffer = cmdBuf;

        VkSubmitInfo2 submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &cmdSubmit;

        vkQueueSubmit2(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_graphicsQueue);

        vkFreeCommandBuffers(m_device, m_cmdPool, 1, &cmdBuf);
        vkDestroyBuffer(m_device, scratchBuffer, nullptr);
        vkFreeMemory(m_device, scratchMemory, nullptr);

        std::cout << "BLAS created\n";

        // Build TLAS (Top Level Acceleration Structure)
        VkAccelerationStructureDeviceAddressInfoKHR asAddrInfo{};
        asAddrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        asAddrInfo.accelerationStructure = m_blas;
        VkDeviceAddress blasAddress = vkGetAccelerationStructureDeviceAddressKHR(m_device, &asAddrInfo);

        VkTransformMatrixKHR transform = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f
        };

        VkAccelerationStructureInstanceKHR instance{};
        memcpy(&instance.transform, &transform, sizeof(transform));
        instance.instanceCustomIndex = 0;
        instance.mask = 0xFF;
        instance.instanceShaderBindingTableRecordOffset = 0;
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instance.accelerationStructureReference = blasAddress;

        // Create instance buffer
        VkBuffer instanceBuffer;
        VkDeviceMemory instanceMemory;
        bci.size = sizeof(VkAccelerationStructureInstanceKHR);
        bci.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        vkCreateBuffer(m_device, &bci, nullptr, &instanceBuffer);
        vkGetBufferMemoryRequirements(m_device, instanceBuffer, &memReq);
        mai.allocationSize = memReq.size;
        mai.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(m_device, &mai, nullptr, &instanceMemory);
        vkBindBufferMemory(m_device, instanceBuffer, instanceMemory, 0);

        void* instanceData;
        vkMapMemory(m_device, instanceMemory, 0, sizeof(instance), 0, &instanceData);
        memcpy(instanceData, &instance, sizeof(instance));
        vkUnmapMemory(m_device, instanceMemory);

        bdai.buffer = instanceBuffer;
        VkDeviceAddress instanceAddress = vkGetBufferDeviceAddressKHR(m_device, &bdai);

        VkAccelerationStructureGeometryKHR tlasGeom{};
        tlasGeom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        tlasGeom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        tlasGeom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        tlasGeom.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        tlasGeom.geometry.instances.arrayOfPointers = VK_FALSE;
        tlasGeom.geometry.instances.data.deviceAddress = instanceAddress;

        VkAccelerationStructureBuildGeometryInfoKHR tlasBuildInfo{};
        tlasBuildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        tlasBuildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        tlasBuildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        tlasBuildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        tlasBuildInfo.geometryCount = 1;
        tlasBuildInfo.pGeometries = &tlasGeom;

        primitiveCount = 1;
        vkGetAccelerationStructureBuildSizesKHR(m_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                 &tlasBuildInfo, &primitiveCount, &sizeInfo);

        // Create TLAS buffer
        bci.size = sizeInfo.accelerationStructureSize;
        bci.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        vkCreateBuffer(m_device, &bci, nullptr, &m_tlasBuffer);
        vkGetBufferMemoryRequirements(m_device, m_tlasBuffer, &memReq);
        mai.allocationSize = memReq.size;
        mai.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(m_device, &mai, nullptr, &m_tlasMemory);
        vkBindBufferMemory(m_device, m_tlasBuffer, m_tlasMemory, 0);

        asci.buffer = m_tlasBuffer;
        asci.size = sizeInfo.accelerationStructureSize;
        asci.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        vkCreateAccelerationStructureKHR(m_device, &asci, nullptr, &m_tlas);

        // Create scratch buffer
        bci.size = sizeInfo.buildScratchSize;
        bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        vkCreateBuffer(m_device, &bci, nullptr, &scratchBuffer);
        vkGetBufferMemoryRequirements(m_device, scratchBuffer, &memReq);
        mai.allocationSize = memReq.size;
        mai.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(m_device, &mai, nullptr, &scratchMemory);
        vkBindBufferMemory(m_device, scratchBuffer, scratchMemory, 0);

        bdai.buffer = scratchBuffer;
        scratchAddress = vkGetBufferDeviceAddressKHR(m_device, &bdai);

        tlasBuildInfo.dstAccelerationStructure = m_tlas;
        tlasBuildInfo.scratchData.deviceAddress = scratchAddress;

        // Build TLAS
        vkAllocateCommandBuffers(m_device, &cmdAllocInfo, &cmdBuf);
        vkBeginCommandBuffer(cmdBuf, &beginInfo);
        vkCmdBuildAccelerationStructuresKHR(cmdBuf, 1, &tlasBuildInfo, &pBuildRange);
        vkEndCommandBuffer(cmdBuf);

        VkCommandBufferSubmitInfo cmdSubmit2{};
        cmdSubmit2.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cmdSubmit2.commandBuffer = cmdBuf;

        VkSubmitInfo2 submitInfo2{};
        submitInfo2.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo2.commandBufferInfoCount = 1;
        submitInfo2.pCommandBufferInfos = &cmdSubmit2;

        vkQueueSubmit2(m_graphicsQueue, 1, &submitInfo2, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_graphicsQueue);

        vkFreeCommandBuffers(m_device, m_cmdPool, 1, &cmdBuf);
        vkDestroyBuffer(m_device, scratchBuffer, nullptr);
        vkFreeMemory(m_device, scratchMemory, nullptr);
        vkDestroyBuffer(m_device, instanceBuffer, nullptr);
        vkFreeMemory(m_device, instanceMemory, nullptr);

        std::cout << "TLAS created\n";
    }

    // 3. Create descriptor set layout
    {
        std::vector<VkDescriptorSetLayoutBinding> bindings;

        // binding 0: storage image
        VkDescriptorSetLayoutBinding binding0{};
        binding0.binding = 0;
        binding0.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        binding0.descriptorCount = 1;
        binding0.stageFlags = m_useRTX ? VK_SHADER_STAGE_RAYGEN_BIT_KHR : VK_SHADER_STAGE_COMPUTE_BIT;
        bindings.push_back(binding0);

        // binding 1: octree nodes buffer
        VkDescriptorSetLayoutBinding binding1{};
        binding1.binding = 1;
        binding1.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding1.descriptorCount = 1;
        binding1.stageFlags = m_useRTX ? (VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_INTERSECTION_BIT_KHR) : VK_SHADER_STAGE_COMPUTE_BIT;
        bindings.push_back(binding1);

        // binding 2: octree colors buffer
        VkDescriptorSetLayoutBinding binding2{};
        binding2.binding = 2;
        binding2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding2.descriptorCount = 1;
        binding2.stageFlags = m_useRTX ? VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR : VK_SHADER_STAGE_COMPUTE_BIT;
        bindings.push_back(binding2);

        // binding 3: acceleration structure (RTX only)
        if (m_useRTX) {
            VkDescriptorSetLayoutBinding binding3{};
            binding3.binding = 3;
            binding3.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            binding3.descriptorCount = 1;
            binding3.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
            bindings.push_back(binding3);
        }

        VkDescriptorSetLayoutCreateInfo dslci{};
        dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.bindingCount = static_cast<uint32_t>(bindings.size());
        dslci.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(m_device, &dslci, nullptr, &m_rtDescSetLayout) != VK_SUCCESS) {
            std::cerr << "vkCreateDescriptorSetLayout failed\n";
            return false;
        }
        DBGPRINT << "Descriptor set layout created\n";
    }

    // 4. Create descriptor pool
    {
        std::vector<VkDescriptorPoolSize> poolSizes;

        VkDescriptorPoolSize poolSize0{};
        poolSize0.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        poolSize0.descriptorCount = 1;
        poolSizes.push_back(poolSize0);

        VkDescriptorPoolSize poolSize1{};
        poolSize1.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSize1.descriptorCount = 2;
        poolSizes.push_back(poolSize1);

        if (m_useRTX) {
            VkDescriptorPoolSize poolSize2{};
            poolSize2.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            poolSize2.descriptorCount = 1;
            poolSizes.push_back(poolSize2);
        }

        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = 1;
        dpci.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        dpci.pPoolSizes = poolSizes.data();

        if (vkCreateDescriptorPool(m_device, &dpci, nullptr, &m_rtDescPool) != VK_SUCCESS) {
            std::cerr << "vkCreateDescriptorPool failed\n";
            return false;
        }
        DBGPRINT << "Descriptor pool created\n";
    }

    // 5. Allocate and update descriptor set
    {
        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = m_rtDescPool;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &m_rtDescSetLayout;

        if (vkAllocateDescriptorSets(m_device, &dsai, &m_rtDescSet) != VK_SUCCESS) {
            std::cerr << "vkAllocateDescriptorSets failed\n";
            return false;
        }
        DBGPRINT << "Descriptor set allocated\n";

        std::vector<VkWriteDescriptorSet> writes;

        // Image write
        VkDescriptorImageInfo imgInfo{};
        imgInfo.imageView = m_rtImageView;
        imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet write0{};
        write0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write0.dstSet = m_rtDescSet;
        write0.dstBinding = 0;
        write0.descriptorCount = 1;
        write0.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write0.pImageInfo = &imgInfo;
        writes.push_back(write0);

        // Octree nodes buffer write
        VkDescriptorBufferInfo nodesBufInfo{};
        nodesBufInfo.buffer = m_octreeNodesBuffer;
        nodesBufInfo.offset = 0;
        nodesBufInfo.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet write1{};
        write1.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write1.dstSet = m_rtDescSet;
        write1.dstBinding = 1;
        write1.descriptorCount = 1;
        write1.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write1.pBufferInfo = &nodesBufInfo;
        writes.push_back(write1);

        // Octree colors buffer write
        VkDescriptorBufferInfo colorsBufInfo{};
        colorsBufInfo.buffer = m_octreeColorsBuffer;
        colorsBufInfo.offset = 0;
        colorsBufInfo.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet write2{};
        write2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write2.dstSet = m_rtDescSet;
        write2.dstBinding = 2;
        write2.descriptorCount = 1;
        write2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write2.pBufferInfo = &colorsBufInfo;
        writes.push_back(write2);

        // Acceleration structure write (RTX only)
        VkWriteDescriptorSetAccelerationStructureKHR asWrite{};
        if (m_useRTX) {
            asWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
            asWrite.accelerationStructureCount = 1;
            asWrite.pAccelerationStructures = &m_tlas;

            VkWriteDescriptorSet write3{};
            write3.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write3.dstSet = m_rtDescSet;
            write3.dstBinding = 3;
            write3.descriptorCount = 1;
            write3.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            write3.pNext = &asWrite;
            writes.push_back(write3);
        }

        vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        DBGPRINT << "Descriptor sets updated\n";
    }

    // Transition storage image to GENERAL for repeated use in compute shaders
    {
        VkCommandBufferAllocateInfo cbai{};
        cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool = m_cmdPool;
        cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;

        VkCommandBuffer transCmd = VK_NULL_HANDLE;
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
        DBGPRINT << "Storage image transitioned to GENERAL\n";
    }

    // 6. Create pipeline (RTX ray tracing or compute fallback)
    if (m_useRTX) {
        // Load ray tracing shaders
        std::vector<char> rgenCode = vox::loadSpv("shaders/raytrace.rgen.spv");
        std::vector<char> rchitCode = vox::loadSpv("shaders/raytrace.rchit.spv");
        std::vector<char> rmissCode = vox::loadSpv("shaders/raytrace.rmiss.spv");
        std::vector<char> rintCode = vox::loadSpv("shaders/raytrace.rint.spv");

        if (rgenCode.empty() || rchitCode.empty() || rmissCode.empty() || rintCode.empty()) {
            std::cerr << "Failed to load ray tracing shaders\n";
            return false;
        }

        VkShaderModule rgenModule = vox::createShaderModule(m_device, rgenCode);
        VkShaderModule rchitModule = vox::createShaderModule(m_device, rchitCode);
        VkShaderModule rmissModule = vox::createShaderModule(m_device, rmissCode);
        VkShaderModule rintModule = vox::createShaderModule(m_device, rintCode);

        // Shader stages
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages(4);

        shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[0].stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        shaderStages[0].module = rgenModule;
        shaderStages[0].pName = "main";

        shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[1].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        shaderStages[1].module = rchitModule;
        shaderStages[1].pName = "main";

        shaderStages[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[2].stage = VK_SHADER_STAGE_MISS_BIT_KHR;
        shaderStages[2].module = rmissModule;
        shaderStages[2].pName = "main";

        shaderStages[3].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[3].stage = VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
        shaderStages[3].module = rintModule;
        shaderStages[3].pName = "main";

        // Shader groups
        std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups(3);

        // Group 0: raygen
        shaderGroups[0].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        shaderGroups[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        shaderGroups[0].generalShader = 0;
        shaderGroups[0].closestHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroups[0].anyHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroups[0].intersectionShader = VK_SHADER_UNUSED_KHR;

        // Group 1: miss
        shaderGroups[1].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        shaderGroups[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        shaderGroups[1].generalShader = 2;
        shaderGroups[1].closestHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroups[1].anyHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroups[1].intersectionShader = VK_SHADER_UNUSED_KHR;

        // Group 2: hit (closest hit + intersection for AABB)
        shaderGroups[2].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        shaderGroups[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
        shaderGroups[2].generalShader = VK_SHADER_UNUSED_KHR;
        shaderGroups[2].closestHitShader = 1;
        shaderGroups[2].anyHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroups[2].intersectionShader = 3;

        // Push constants
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        pushRange.offset = 0;
        pushRange.size = sizeof(float) * 6; // time + debugMask + distance + yaw + pitch + fov

        // Pipeline layout
        VkPipelineLayoutCreateInfo  plci{};
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount = 1;
        plci.pSetLayouts = &m_rtDescSetLayout;
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges = &pushRange;

        if (vkCreatePipelineLayout(m_device, &plci, nullptr, &m_rtPipelineLayout) != VK_SUCCESS) {
            std::cerr << "Failed to create RT pipeline layout\n";
            return false;
        }

        // Ray tracing pipeline
        VkRayTracingPipelineCreateInfoKHR rtpci{};
        rtpci.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
        rtpci.stageCount = static_cast<uint32_t>(shaderStages.size());
        rtpci.pStages = shaderStages.data();
        rtpci.groupCount = static_cast<uint32_t>(shaderGroups.size());
        rtpci.pGroups = shaderGroups.data();
        rtpci.maxPipelineRayRecursionDepth = 1;
        rtpci.layout = m_rtPipelineLayout;

        if (vkCreateRayTracingPipelinesKHR(m_device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rtpci, nullptr, &m_rtPipeline) != VK_SUCCESS) {
            std::cerr << "Failed to create RT pipeline\n";
            return false;
        }

        std::cout << "Ray tracing pipeline created\n";

        // Create shader binding table
        uint32_t handleSize = m_rtPipelineProperties.shaderGroupHandleSize;
        uint32_t handleAlignment = m_rtPipelineProperties.shaderGroupHandleAlignment;
        uint32_t baseAlignment = m_rtPipelineProperties.shaderGroupBaseAlignment;

        uint32_t handleSizeAligned = (handleSize + handleAlignment - 1) & ~(handleAlignment - 1);

        uint32_t rgenStride = (handleSizeAligned + baseAlignment - 1) & ~(baseAlignment - 1);
        uint32_t missStride = handleSizeAligned;
        uint32_t hitStride = handleSizeAligned;

        uint32_t rgenSize = rgenStride;
        uint32_t missSize = missStride;
        uint32_t hitSize = hitStride;

        VkDeviceSize sbtSize = rgenSize + missSize + hitSize;

        // Get shader group handles
        std::vector<uint8_t> handleData(3 * handleSize);
        if (vkGetRayTracingShaderGroupHandlesKHR(m_device, m_rtPipeline, 0, 3, handleData.size(), handleData.data()) != VK_SUCCESS) {
            std::cerr << "Failed to get RT shader group handles\n";
            return false;
        }

        // Create SBT buffer
        VkBufferCreateInfo bci{};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = sbtSize;
        bci.usage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

        if (vkCreateBuffer(m_device, &bci, nullptr, &m_sbtBuffer) != VK_SUCCESS) {
            std::cerr << "Failed to create SBT buffer\n";
            return false;
        }

        VkMemoryRequirements memReq{};
        vkGetBufferMemoryRequirements(m_device, m_sbtBuffer, &memReq);

        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);
        auto findMemoryType = [&](uint32_t typeFilter, VkMemoryPropertyFlags props) -> uint32_t {
            for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
                if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props) return i;
            }
            return 0;
        };

        VkMemoryAllocateFlagsInfo allocFlags{};
        allocFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        allocFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = memReq.size;
        mai.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        mai.pNext = &allocFlags;

        if (vkAllocateMemory(m_device, &mai, nullptr, &m_sbtMemory) != VK_SUCCESS) {
            std::cerr << "Failed to allocate SBT memory\n";
            return false;
        }

        vkBindBufferMemory(m_device, m_sbtBuffer, m_sbtMemory, 0);

        // Map and fill SBT
        void* sbtData;
        vkMapMemory(m_device, m_sbtMemory, 0, sbtSize, 0, &sbtData);

        uint8_t* sbtBytes = reinterpret_cast<uint8_t*>(sbtData);
        memcpy(sbtBytes, handleData.data(), handleSize); // raygen
        memcpy(sbtBytes + rgenSize, handleData.data() + handleSize, handleSize); // miss
        memcpy(sbtBytes + rgenSize + missSize, handleData.data() + 2 * handleSize, handleSize); // hit

        vkUnmapMemory(m_device, m_sbtMemory);

        // Get SBT buffer device address
        VkBufferDeviceAddressInfo bdai{};
        bdai.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        bdai.buffer = m_sbtBuffer;
        VkDeviceAddress sbtAddress = vkGetBufferDeviceAddressKHR(m_device, &bdai);

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

        std::cout << "Shader binding table created\n";

        // Cleanup shader modules
        vkDestroyShaderModule(m_device, rgenModule, nullptr);
        vkDestroyShaderModule(m_device, rchitModule, nullptr);
        vkDestroyShaderModule(m_device, rmissModule, nullptr);
        vkDestroyShaderModule(m_device, rintModule, nullptr);
    } else {
        // Compute shader fallback
        std::vector<char> compCode = vox::loadSpv("shaders/raytrace.comp.spv");
        if (compCode.empty()) {
            std::cerr << "Failed to load compute shader SPIR-V\n";
            return false;
        }

        VkShaderModule compModule = vox::createShaderModule(m_device, compCode);
        if (compModule == VK_NULL_HANDLE) {
            std::cerr << "Compute shader module creation failed\n";
            return false;
        }
        DBGPRINT << "Compute shader module created\n";

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(float) * 5 + sizeof(uint32_t); // time + (orbitRadius, camHeight, fov, orbitAngle) + debugMask (total 24 bytes)

        VkPipelineLayoutCreateInfo plci{};
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount = 1;
        plci.pSetLayouts = &m_rtDescSetLayout;
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges = &pushRange;

        if (vkCreatePipelineLayout(m_device, &plci, nullptr, &m_rtPipelineLayout) != VK_SUCCESS) {
            std::cerr << "vkCreatePipelineLayout (compute) failed\n";
            return false;
        }
        DBGPRINT << "Compute pipeline layout created\n";

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = compModule;
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo cpci{};
        cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpci.layout = m_rtPipelineLayout;
        cpci.stage = stageInfo;

        if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &cpci, nullptr, &m_rtPipeline) != VK_SUCCESS) {
            std::cerr << "vkCreateComputePipelines failed\n";
            return false;
        }
        DBGPRINT << "Compute pipeline created\n";

        vkDestroyShaderModule(m_device, compModule, nullptr);
    }

    m_initialized = true;

    // initialize runtime timer
    m_startTime = std::chrono::high_resolution_clock::now();

    return true;
}

} // namespace vox
