#include "vox/graphics/VulkanDevice.h"
#include <iostream>
#include <vector>
#include <cstring>
#include <algorithm>

// Define extension names if not available
#ifndef VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME
#define VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME "VK_KHR_wayland_surface"
#endif
#ifndef VK_KHR_XCB_SURFACE_EXTENSION_NAME
#define VK_KHR_XCB_SURFACE_EXTENSION_NAME "VK_KHR_xcb_surface"
#endif
#ifndef VK_KHR_XLIB_SURFACE_EXTENSION_NAME
#define VK_KHR_XLIB_SURFACE_EXTENSION_NAME "VK_KHR_xlib_surface"
#endif

namespace vox {

static bool hasExtension(const std::vector<VkExtensionProperties>& exts, const char* name) {
    for (auto &e : exts) if (std::string(e.extensionName) == name) return true;
    return false;
}

VulkanDevice::VulkanDevice() {}

VulkanDevice::~VulkanDevice() {
    if (m_cmdPool != VK_NULL_HANDLE) vkDestroyCommandPool(m_device, m_cmdPool, nullptr);
    if (m_device != VK_NULL_HANDLE) vkDestroyDevice(m_device, nullptr);
    if (m_instance != VK_NULL_HANDLE) vkDestroyInstance(m_instance, nullptr);
}

bool VulkanDevice::init(VkSurfaceKHR surface) {
    if (!createInstance()) return false;
    if (!selectPhysicalDevice(surface)) return false;
    if (!createLogicalDevice()) return false;
    if (!checkRTXSupport()) std::cout << "RTX not supported - fallback to compute shader\n";
    
    // Create command pool
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = m_graphicsQueueFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    
    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_cmdPool) != VK_SUCCESS) {
        std::cerr << "Failed to create command pool\n";
        return false;
    }
    
    return true;
}

bool VulkanDevice::createInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "vox";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_4;

    // Query available extensions
    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data());

    // Required extensions
    std::vector<const char*> extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
    };

    // Add platform-specific surface extensions if available
    if (hasExtension(availableExtensions, VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
    }
    if (hasExtension(availableExtensions, VK_KHR_XCB_SURFACE_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
    }
    if (hasExtension(availableExtensions, VK_KHR_XLIB_SURFACE_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
    }

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledExtensionCount = extensions.size();

    return vkCreateInstance(&createInfo, nullptr, &m_instance) == VK_SUCCESS;
}

bool VulkanDevice::selectPhysicalDevice(VkSurfaceKHR surface) {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (count == 0) {
        std::cerr << "No Vulkan devices found\n";
        return false;
    }

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

    for (auto dev : devices) {
        uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, nullptr);
        std::vector<VkQueueFamilyProperties> qProps(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, qProps.data());

        for (uint32_t i = 0; i < qCount; ++i) {
            VkBool32 canPresent = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface, &canPresent);
            if ((qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && canPresent) {
                m_physicalDevice = dev;
                m_graphicsQueueFamily = i;
                vkGetPhysicalDeviceMemoryProperties(dev, &m_memProps);
                return true;
            }
        }
    }

    return false;
}

bool VulkanDevice::createLogicalDevice() {
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = m_graphicsQueueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    // Check for required extensions
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> exts(extCount);
    vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extCount, exts.data());

    std::vector<const char*> enabledExts = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    
    bool hasRTX = hasExtension(exts, "VK_KHR_ray_tracing_pipeline") &&
                  hasExtension(exts, "VK_KHR_acceleration_structure") &&
                  hasExtension(exts, "VK_KHR_buffer_device_address");

    if (hasRTX) {
        enabledExts.push_back("VK_KHR_ray_tracing_pipeline");
        enabledExts.push_back("VK_KHR_acceleration_structure");
        enabledExts.push_back("VK_KHR_deferred_host_operations");
        enabledExts.push_back("VK_KHR_buffer_device_address");
        enabledExts.push_back("VK_KHR_spirv_1_4");
        enabledExts.push_back("VK_KHR_shader_float_controls");
    }

    VkPhysicalDeviceVulkan12Features vulkan12Features{};
    vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vulkan12Features.timelineSemaphore = VK_TRUE;

    VkPhysicalDeviceVulkan13Features vulkan13Features{};
    vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vulkan13Features.dynamicRendering = VK_TRUE;
    vulkan13Features.synchronization2 = VK_TRUE;
    vulkan13Features.pNext = &vulkan12Features;

    VkPhysicalDeviceBufferDeviceAddressFeatures bufAddrFeatures{};
    bufAddrFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    bufAddrFeatures.bufferDeviceAddress = VK_TRUE;
    bufAddrFeatures.pNext = &vulkan13Features;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtFeatures{};
    rtFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    rtFeatures.rayTracingPipeline = VK_TRUE;
    rtFeatures.pNext = &bufAddrFeatures;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeatures{};
    accelFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    accelFeatures.accelerationStructure = VK_TRUE;
    accelFeatures.pNext = &rtFeatures;

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = enabledExts.size();
    deviceInfo.ppEnabledExtensionNames = enabledExts.data();
    if (hasRTX) deviceInfo.pNext = &accelFeatures;
    else deviceInfo.pNext = &vulkan13Features;

    if (vkCreateDevice(m_physicalDevice, &deviceInfo, nullptr, &m_device) != VK_SUCCESS) {
        std::cerr << "Failed to create logical device\n";
        return false;
    }

    vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
    return true;
}

bool VulkanDevice::checkRTXSupport() {
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> exts(extCount);
    vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extCount, exts.data());

    m_supportsRTX = hasExtension(exts, "VK_KHR_ray_tracing_pipeline") &&
                    hasExtension(exts, "VK_KHR_acceleration_structure");

    if (m_supportsRTX) {
        loadRTXFunctionPointers();
        
        m_rtPipelineProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
        VkPhysicalDeviceProperties2 props{};
        props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props.pNext = &m_rtPipelineProps;
        
        PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2 = 
            (PFN_vkGetPhysicalDeviceProperties2)vkGetInstanceProcAddr(m_instance, "vkGetPhysicalDeviceProperties2");
        
        if (vkGetPhysicalDeviceProperties2) {
            vkGetPhysicalDeviceProperties2(m_physicalDevice, &props);
        }
    }

    return m_supportsRTX;
}

void VulkanDevice::loadRTXFunctionPointers() {
    getBufferDeviceAddressKHR = (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(m_device, "vkGetBufferDeviceAddressKHR");
    createAccelerationStructureKHR = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(m_device, "vkCreateAccelerationStructureKHR");
    destroyAccelerationStructureKHR = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(m_device, "vkDestroyAccelerationStructureKHR");
    getAccelerationStructureBuildSizesKHR = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(m_device, "vkGetAccelerationStructureBuildSizesKHR");
    getAccelerationStructureDeviceAddressKHR = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(m_device, "vkGetAccelerationStructureDeviceAddressKHR");
    cmdBuildAccelerationStructuresKHR = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(m_device, "vkCmdBuildAccelerationStructuresKHR");
    buildAccelerationStructuresKHR = (PFN_vkBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(m_device, "vkBuildAccelerationStructuresKHR");
    createRayTracingPipelinesKHR = (PFN_vkCreateRayTracingPipelinesKHR)vkGetDeviceProcAddr(m_device, "vkCreateRayTracingPipelinesKHR");
    getRayTracingShaderGroupHandlesKHR = (PFN_vkGetRayTracingShaderGroupHandlesKHR)vkGetDeviceProcAddr(m_device, "vkGetRayTracingShaderGroupHandlesKHR");
    cmdTraceRaysKHR = (PFN_vkCmdTraceRaysKHR)vkGetDeviceProcAddr(m_device, "vkCmdTraceRaysKHR");
}

uint32_t VulkanDevice::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) {
    for (uint32_t i = 0; i < m_memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) && (m_memProps.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    return 0;
}

} // namespace vox
