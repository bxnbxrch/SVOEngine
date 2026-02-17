#include "vox/VulkanRenderer.h"
#include "vox/SparseVoxelOctree.h"
#include <SDL_vulkan.h>
#include <iostream>
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <sstream>
#include <chrono>
#include <cmath>
#include "vox/Shader.h"

// Debug printing: use like normal cout when DEBUG is defined, no-op otherwise
#ifdef DEBUG
struct DebugStream {
    std::ostringstream oss;
    template<typename T>
    DebugStream& operator<<(const T& x) {
        oss << x;
        return *this;
    }
    ~DebugStream() {
        if (!oss.str().empty()) {
            std::cout << oss.str() << std::flush;
        }
    }
};
#define DBGPRINT DebugStream()
#else
struct NullStream {
    template<typename T>
    NullStream& operator<<(const T&) { return *this; }
};
#define DBGPRINT NullStream()
#endif

using namespace vox;

static bool hasExtension(const std::vector<VkExtensionProperties>& exts, const char* name) {
    for (auto &e : exts) if (std::string(e.extensionName) == name) return true;
    return false;
}

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

    if (m_imgAvail != VK_NULL_HANDLE) vkDestroySemaphore(m_device, m_imgAvail, nullptr);
    if (m_renderDone != VK_NULL_HANDLE) vkDestroySemaphore(m_device, m_renderDone, nullptr);
    if (m_cmdPool != VK_NULL_HANDLE) vkDestroyCommandPool(m_device, m_cmdPool, nullptr);
    for (auto fb : m_framebuffers) vkDestroyFramebuffer(m_device, fb, nullptr);

    if (m_renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(m_device, m_renderPass, nullptr);
    for (auto iv : m_imageViews) vkDestroyImageView(m_device, iv, nullptr);

    if (m_swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
    if (m_device != VK_NULL_HANDLE) vkDestroyDevice(m_device, nullptr);
    if (m_surface != VK_NULL_HANDLE) vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    if (m_instance != VK_NULL_HANDLE) vkDestroyInstance(m_instance, nullptr);
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
    appInfo.apiVersion = VK_API_VERSION_1_0;

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

    // check swapchain extension only
    uint32_t devExtCount = 0;
    vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &devExtCount, nullptr);
    std::vector<VkExtensionProperties> devExts(devExtCount);
    vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &devExtCount, devExts.data());
    bool hasSwap = hasExtension(devExts, VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    if (!hasSwap) {
        std::cerr << "GPU lacks VK_KHR_swapchain" << std::endl;
        return false;
    }

    DBGPRINT << "Using compute shader ray tracing (portable fallback)\n";

    float qprio = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = m_graphicsQueueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &qprio;

    const char* devExtsReq[] = { 
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };
    uint32_t devExtReqCount = 1;

    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = devExtReqCount;
    dci.ppEnabledExtensionNames = devExtsReq;

    if (vkCreateDevice(m_physicalDevice, &dci, nullptr, &m_device) != VK_SUCCESS) {
        std::cerr << "vkCreateDevice failed" << std::endl;
        return false;
    }
    vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);

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
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
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

    // render pass
    {
        VkAttachmentDescription colorAtt{};
        colorAtt.format = m_surfaceFormat;
        colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAtt.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        VkRenderPassCreateInfo rpci{};
        rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpci.attachmentCount = 1;
        rpci.pAttachments = &colorAtt;
        rpci.subpassCount = 1;
        rpci.pSubpasses = &subpass;

        vkCreateRenderPass(m_device, &rpci, nullptr, &m_renderPass);
    }

    // framebuffers
    m_framebuffers.resize(m_imageViews.size());
    for (uint32_t i = 0; i < m_imageViews.size(); ++i) {
        VkFramebufferCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = m_renderPass;
        fci.attachmentCount = 1;
        fci.pAttachments = &m_imageViews[i];
        fci.width = m_extent.width;
        fci.height = m_extent.height;
        fci.layers = 1;
        vkCreateFramebuffer(m_device, &fci, nullptr, &m_framebuffers[i]);
    }

    // command pool + buffers
    VkCommandPoolCreateInfo pc{};
    pc.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pc.queueFamilyIndex = m_graphicsQueueFamily;
    pc.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCreateCommandPool(m_device, &pc, nullptr, &m_cmdPool);

    m_cmdBuffers.resize(m_framebuffers.size());
    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = m_cmdPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = static_cast<uint32_t>(m_cmdBuffers.size());
    vkAllocateCommandBuffers(m_device, &cbai, m_cmdBuffers.data());

    // record command buffers to clear screen
    for (uint32_t i = 0; i < m_cmdBuffers.size(); ++i) {
        VkCommandBufferBeginInfo binfo{};
        binfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(m_cmdBuffers[i], &binfo);

        VkClearValue clearColor = {{{0.05f, 0.05f, 0.08f, 1.0f}}};
        VkRenderPassBeginInfo rpbi{};
        rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpbi.renderPass = m_renderPass;
        rpbi.framebuffer = m_framebuffers[i];
        rpbi.renderArea.offset = {0, 0};
        rpbi.renderArea.extent = m_extent;
        rpbi.clearValueCount = 1;
        rpbi.pClearValues = &clearColor;

        vkCmdBeginRenderPass(m_cmdBuffers[i], &rpbi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdEndRenderPass(m_cmdBuffers[i]);
        vkEndCommandBuffer(m_cmdBuffers[i]);
    }

    VkSemaphoreCreateInfo semci{};
    semci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    vkCreateSemaphore(m_device, &semci, nullptr, &m_imgAvail);
    vkCreateSemaphore(m_device, &semci, nullptr, &m_renderDone);

    // Initialize octree
    m_octree = std::make_unique<SparseVoxelOctree>(8);
    m_octree->generateTestScene();
    DBGPRINT << "Octree initialized with test scene\n";
    DBGPRINT << "  Nodes: " << m_octree->getNodes().size() << "\n";
    DBGPRINT << "  Colors: " << m_octree->getColors().size() << "\n";

    // === Setup compute shader ray tracing ===
    
    // 1. Create storage image (will hold raytrace output)
    {
        VkImageCreateInfo ici{};
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = VK_FORMAT_R8G8B8A8_UNORM;  // Use RGBA format for compute compatibility
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
        ivci.format = VK_FORMAT_R8G8B8A8_UNORM;  // Match storage image format
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
        DBGPRINT << "Octree GPU buffers created\n";    }

    // 3. Create descriptor set layout
    {
        VkDescriptorSetLayoutBinding bindings[3]{};
        
        // binding 0: storage image
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        
        // binding 1: octree nodes buffer
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        
        // binding 2: octree colors buffer
        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo dslci{};
        dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.bindingCount = 3;
        dslci.pBindings = bindings;

        if (vkCreateDescriptorSetLayout(m_device, &dslci, nullptr, &m_rtDescSetLayout) != VK_SUCCESS) {
            std::cerr << "vkCreateDescriptorSetLayout failed\n";
            return false;
        }
        DBGPRINT << "Descriptor set layout created\n";
    }

    // 4. Create descriptor pool
    {
        VkDescriptorPoolSize poolSizes[3]{};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        poolSizes[0].descriptorCount = 1;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSizes[1].descriptorCount = 2;

        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets = 1;
        dpci.poolSizeCount = 2;
        dpci.pPoolSizes = poolSizes;

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

        VkWriteDescriptorSet writes[3]{};

        // Image write
        VkDescriptorImageInfo imgInfo{};
        imgInfo.imageView = m_rtImageView;
        imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = m_rtDescSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].pImageInfo = &imgInfo;

        // Octree nodes buffer write
        VkDescriptorBufferInfo nodesBufInfo{};
        nodesBufInfo.buffer = m_octreeNodesBuffer;
        nodesBufInfo.offset = 0;
        nodesBufInfo.range = VK_WHOLE_SIZE;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = m_rtDescSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].pBufferInfo = &nodesBufInfo;

        // Octree colors buffer write
        VkDescriptorBufferInfo colorsBufInfo{};
        colorsBufInfo.buffer = m_octreeColorsBuffer;
        colorsBufInfo.offset = 0;
        colorsBufInfo.range = VK_WHOLE_SIZE;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = m_rtDescSet;
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].pBufferInfo = &colorsBufInfo;

        vkUpdateDescriptorSets(m_device, 3, writes, 0, nullptr);
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

        VkImageMemoryBarrier imb{};
        imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imb.image = m_rtImage;
        imb.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imb.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        imb.srcAccessMask = 0;
        imb.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imb.subresourceRange.baseMipLevel = 0;
        imb.subresourceRange.levelCount = 1;
        imb.subresourceRange.baseArrayLayer = 0;
        imb.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(transCmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &imb);

        vkEndCommandBuffer(transCmd);

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &transCmd;
        vkQueueSubmit(m_graphicsQueue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_graphicsQueue);

        vkFreeCommandBuffers(m_device, m_cmdPool, 1, &transCmd);
        DBGPRINT << "Storage image transitioned to GENERAL\n";
    }

    // 6. Load compute shader and create pipeline
    {
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

void VulkanRenderer::toggleGridOverlay() {
    m_showSvoOverlay = !m_showSvoOverlay;
    std::cout << "SVO overlay " << (m_showSvoOverlay ? "ON" : "OFF") << "\n";
}

void VulkanRenderer::drawFrame() {
    if (!m_initialized) return;

    DBGPRINT << "drawFrame: acquiring image\n";
    uint32_t imgIndex = 0;
    VkResult acqRes = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, m_imgAvail, VK_NULL_HANDLE, &imgIndex);
    if (acqRes != VK_SUCCESS && acqRes != VK_SUBOPTIMAL_KHR) {
        std::cerr << "vkAcquireNextImageKHR failed with result " << acqRes << std::endl;
        return;
    }
    DBGPRINT << "drawFrame: got image " << imgIndex << "\n";

    // Re-record compute command buffer for this frame
    DBGPRINT << "drawFrame: resetting command buffer\n";
    vkResetCommandBuffer(m_cmdBuffers[imgIndex], 0);
    DBGPRINT << "drawFrame: command buffer reset\n";
    
    VkCommandBufferBeginInfo cbbi{};
    cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    DBGPRINT << "drawFrame: beginning command buffer\n";
    vkBeginCommandBuffer(m_cmdBuffers[imgIndex], &cbbi);
    DBGPRINT << "drawFrame: command buffer begun\n";

    // Dispatch compute shader (storage image already in GENERAL layout)
    vkCmdBindPipeline(m_cmdBuffers[imgIndex], VK_PIPELINE_BIND_POINT_COMPUTE, m_rtPipeline);
    DBGPRINT << "drawFrame: pipeline bound\n";
    vkCmdBindDescriptorSets(m_cmdBuffers[imgIndex], VK_PIPELINE_BIND_POINT_COMPUTE, m_rtPipelineLayout,
                            0, 1, &m_rtDescSet, 0, nullptr);
    DBGPRINT << "drawFrame: descriptor sets bound\n";

    // Push time for camera orbit + debug mask + orbit params
    struct PC { float time; uint32_t debugMask; float orbitRadius; float camHeight; float fov; float orbitAngle; } pc;

    auto nowTime = std::chrono::high_resolution_clock::now();
    if (m_pauseOrbit) {
        pc.time = m_pausedTime;
    } else {
        if (m_startTime.time_since_epoch().count() == 0) m_startTime = nowTime;
        pc.time = std::chrono::duration<float>(nowTime - m_startTime).count();
    }

    // debugMask: bit0 = show subgrids, bit1 = show root bounds
    pc.debugMask = m_showSvoOverlay ? 3u : 0u;

    // ensure orbit radius respects safety minimum
    const float GRID_SIZE = 256.0f;
    float halfDiag = std::sqrt(3.0f) * (GRID_SIZE * 0.5f);
    float desiredDist = halfDiag * 1.2f;
    float minOrbit = std::sqrt(std::max(0.0f, desiredDist * desiredDist - m_camHeight * m_camHeight));
    pc.orbitRadius = std::max(m_orbitRadius, minOrbit);
    pc.camHeight = m_camHeight;
    pc.fov = m_fov;
    pc.orbitAngle = m_orbitAngle;

    vkCmdPushConstants(m_cmdBuffers[imgIndex], m_rtPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

    uint32_t groupCountX = (m_extent.width + 7) / 8;
    uint32_t groupCountY = (m_extent.height + 7) / 8;
    DBGPRINT << "drawFrame: dispatching " << groupCountX << "x" << groupCountY << " groups\n";
    vkCmdDispatch(m_cmdBuffers[imgIndex], groupCountX, groupCountY, 1);
    DBGPRINT << "drawFrame: dispatch done\n";

    // Barrier: wait for compute to finish, transition for transfer
    {
        DBGPRINT << "drawFrame: creating memory barrier 1\n";
        VkImageMemoryBarrier imb{};
        imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imb.image = m_rtImage;
        imb.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        imb.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        imb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        imb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imb.subresourceRange.baseMipLevel = 0;
        imb.subresourceRange.levelCount = 1;
        imb.subresourceRange.baseArrayLayer = 0;
        imb.subresourceRange.layerCount = 1;

        DBGPRINT << "drawFrame: issuing pipeline barrier 1\n";
        vkCmdPipelineBarrier(m_cmdBuffers[imgIndex], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &imb);
        DBGPRINT << "drawFrame: barrier 1 issued\n";
    }

    // Transition swapchain image to TRANSFER_DST
    {
        DBGPRINT << "drawFrame: creating memory barrier 2\n";
        VkImageMemoryBarrier imb{};
        imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imb.image = m_swapImages[imgIndex];
        imb.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imb.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imb.srcAccessMask = 0;
        imb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imb.subresourceRange.baseMipLevel = 0;
        imb.subresourceRange.levelCount = 1;
        imb.subresourceRange.baseArrayLayer = 0;
        imb.subresourceRange.layerCount = 1;

        DBGPRINT << "drawFrame: issuing pipeline barrier 2\n";
        vkCmdPipelineBarrier(m_cmdBuffers[imgIndex], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &imb);
        DBGPRINT << "drawFrame: barrier 2 issued\n";
    }

    // Copy/blit raytrace output to swapchain image
    {
        DBGPRINT << "drawFrame: setting up image blit region\n";
        DBGPRINT << "  m_rtImage (RGBA format) to m_swapImages[" << imgIndex << "] (BGRA format)\n";
        
        VkImageBlit region;
        std::memset(&region, 0, sizeof(region));
        
        // Source: full rt image
        region.srcOffsets[0] = {0, 0, 0};
        region.srcOffsets[1] = {(int32_t)m_extent.width, (int32_t)m_extent.height, 1};
        region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.srcSubresource.mipLevel = 0;
        region.srcSubresource.baseArrayLayer = 0;
        region.srcSubresource.layerCount = 1;
        
        // Destination: full swapchain image
        region.dstOffsets[0] = {0, 0, 0};
        region.dstOffsets[1] = {(int32_t)m_extent.width, (int32_t)m_extent.height, 1};
        region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.dstSubresource.mipLevel = 0;
        region.dstSubresource.baseArrayLayer = 0;
        region.dstSubresource.layerCount = 1;
        
        DBGPRINT << "drawFrame: issuing blit image command\n";
        std::cout.flush();
        vkCmdBlitImage(m_cmdBuffers[imgIndex], m_rtImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       m_swapImages[imgIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_NEAREST);
        DBGPRINT << "drawFrame: blit issued\n";
        std::cout.flush();
    }

    // Transition swapchain image to PRESENT_SRC
    {
        DBGPRINT << "drawFrame: creating memory barrier 3\n";
        VkImageMemoryBarrier imb{};
        imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imb.image = m_swapImages[imgIndex];
        imb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imb.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        imb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        imb.dstAccessMask = 0;
        imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imb.subresourceRange.baseMipLevel = 0;
        imb.subresourceRange.levelCount = 1;
        imb.subresourceRange.baseArrayLayer = 0;
        imb.subresourceRange.layerCount = 1;

        DBGPRINT << "drawFrame: issuing pipeline barrier 3\n";
        vkCmdPipelineBarrier(m_cmdBuffers[imgIndex], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &imb);
        DBGPRINT << "drawFrame: barrier 3 issued\n";
    }

    // Transition storage image back to GENERAL for next frame
    {
        DBGPRINT << "drawFrame: creating memory barrier 4\n";
        VkImageMemoryBarrier imb{};
        imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imb.image = m_rtImage;
        imb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        imb.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        imb.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        imb.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imb.subresourceRange.baseMipLevel = 0;
        imb.subresourceRange.levelCount = 1;
        imb.subresourceRange.baseArrayLayer = 0;
        imb.subresourceRange.layerCount = 1;

        DBGPRINT << "drawFrame: issuing pipeline barrier 4\n";
        vkCmdPipelineBarrier(m_cmdBuffers[imgIndex], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &imb);
        DBGPRINT << "drawFrame: barrier 4 issued\n";
    }

    DBGPRINT << "drawFrame: ending command buffer\n";
    vkEndCommandBuffer(m_cmdBuffers[imgIndex]);
    DBGPRINT << "drawFrame: command buffer ended\n";

    // Submit command buffer
    DBGPRINT << "drawFrame: creating submit info\n";
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkSemaphore waitSemaphores[] = { m_imgAvail };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT };
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = waitSemaphores;
    submit.pWaitDstStageMask = waitStages;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &m_cmdBuffers[imgIndex];
    VkSemaphore signalSemaphores[] = { m_renderDone };
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = signalSemaphores;

    DBGPRINT << "drawFrame: submitting to queue\n";
    vkQueueSubmit(m_graphicsQueue, 1, &submit, VK_NULL_HANDLE);
    DBGPRINT << "drawFrame: queue submit done\n";

    // Present
    DBGPRINT << "drawFrame: creating present info\n";
    VkPresentInfoKHR pres{};
    pres.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pres.waitSemaphoreCount = 1;
    pres.pWaitSemaphores = signalSemaphores;
    pres.swapchainCount = 1;
    pres.pSwapchains = &m_swapchain;
    pres.pImageIndices = &imgIndex;
    DBGPRINT << "drawFrame: presenting to queue\n";
    vkQueuePresentKHR(m_graphicsQueue, &pres);
    DBGPRINT << "drawFrame: present done\n";

    DBGPRINT << "drawFrame: waiting for queue idle\n";
    vkQueueWaitIdle(m_graphicsQueue);

    // --- FPS counter: print to console every second (always enabled) ---
    static uint32_t frameCount = 0;
    static auto lastFpsTime = std::chrono::high_resolution_clock::now();
    frameCount++;
    auto nowFps = std::chrono::high_resolution_clock::now();
    float elapsed = std::chrono::duration<float>(nowFps - lastFpsTime).count();
    if (elapsed >= 1.0f) {
        // compute camera pos (matches shader logic)
        const float GRID_SIZE = 256.0f;
        const glm::vec3 orbitCenter = glm::vec3(128.0f);
        const float camHeight = 120.0f;
        float halfDiag = std::sqrt(3.0f) * (GRID_SIZE * 0.5f);
        float desiredDist = halfDiag * 1.2f;
        float minOrbitRadius = std::sqrt(std::max(0.0f, desiredDist * desiredDist - camHeight * camHeight));
        float orbitRadius = std::max(400.0f, minOrbitRadius);
        float angle = pc.time * 0.5f;
        glm::vec3 camPos = orbitCenter + glm::vec3(std::sin(angle) * orbitRadius, camHeight, std::cos(angle) * orbitRadius);

        bool inside = (camPos.x >= 0.0f && camPos.x < GRID_SIZE && camPos.y >= 0.0f && camPos.y < GRID_SIZE && camPos.z >= 0.0f && camPos.z < GRID_SIZE);

        std::cout << "FPS: " << frameCount / elapsed << "  |  cam=(" << camPos.x << "," << camPos.y << "," << camPos.z << ")" << "  insideSVO=" << (inside ? "YES" : "NO") << "\n";
        frameCount = 0;
        lastFpsTime = nowFps;
    }

    DBGPRINT << "drawFrame: complete!\n";
}

void VulkanRenderer::adjustOrbitRadius(float delta) {
    m_orbitRadius += delta;
    // clamp minimum so camera stays outside SVO
    const float GRID_SIZE = 256.0f;
    float halfDiag = std::sqrt(3.0f) * (GRID_SIZE * 0.5f);
    float desiredDist = halfDiag * 1.2f;
    float minOrbit = std::sqrt(std::max(0.0f, desiredDist * desiredDist - m_camHeight * m_camHeight));
    if (m_orbitRadius < minOrbit) m_orbitRadius = minOrbit;
}

void VulkanRenderer::adjustCamHeight(float delta) {
    m_camHeight = std::max(1.0f, m_camHeight + delta);
    // ensure orbit radius still valid
    adjustOrbitRadius(0.0f);
}

void VulkanRenderer::togglePauseOrbit() {
    auto now = std::chrono::high_resolution_clock::now();
    if (!m_pauseOrbit) {
        // pause -> capture current time
        m_pausedTime = std::chrono::duration<float>(now - m_startTime).count();
        m_pauseOrbit = true;
        std::cout << "Orbit paused\n";
    } else {
        // resume -> shift startTime so elapsed continues
        m_startTime = now - std::chrono::duration_cast<std::chrono::high_resolution_clock::duration>(std::chrono::duration<float>(m_pausedTime));
        m_pauseOrbit = false;
        std::cout << "Orbit resumed\n";
    }
}

void VulkanRenderer::adjustOrbitAngle(float deltaRadians) {
    m_orbitAngle += deltaRadians;
    // wrap
    const float twoPi = 6.28318530718f;
    while (m_orbitAngle < 0.0f) m_orbitAngle += twoPi;
    while (m_orbitAngle >= twoPi) m_orbitAngle -= twoPi;
    m_manualOrbit = true;
    // pause time-driven orbit when user manually controls
    if (!m_pauseOrbit) togglePauseOrbit();
}
