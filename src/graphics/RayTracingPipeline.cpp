#include "vox/graphics/RayTracingPipeline.h"
#include "vox/graphics/VulkanDevice.h"
#include "vox/graphics/VulkanShader.h"
#include "vox/graphics/DescriptorSet.h"
#include "vox/graphics/VulkanImage.h"
#include "vox/Shader.h"
#include "vox/raytracing/AccelerationStructure.h"
#include "vox/raytracing/ShaderBindingTable.h"
#include "vox/scene/Scene.h"
#include "vox/scene/Camera.h"
#include <iostream>
#include <vector>

namespace vox {

RayTracingPipeline::RayTracingPipeline(VulkanDevice* device) : Pipeline(device) {}

RayTracingPipeline::~RayTracingPipeline() {}

bool RayTracingPipeline::init() {
    // Check RTX support
    if (!m_device->supportsRayTracing()) {
        std::cerr << "Ray tracing not supported on this GPU\n";
        return false;
    }

    // Load ray tracing shaders
    std::vector<char> rgenCode = vox::loadSpv("shaders/raytrace.rgen.spv");
    std::vector<char> rchitCode = vox::loadSpv("shaders/raytrace.rchit.spv");
    std::vector<char> rmissCode = vox::loadSpv("shaders/raytrace.rmiss.spv");
    std::vector<char> rintCode = vox::loadSpv("shaders/raytrace.rint.spv");
    
    if (rgenCode.empty() || rchitCode.empty() || rmissCode.empty() || rintCode.empty()) {
        std::cerr << "Failed to load ray tracing shaders\n";
        return false;
    }
    
    VkShaderModule rgenModule = vox::createShaderModule(m_device->device(), rgenCode);
    VkShaderModule rchitModule = vox::createShaderModule(m_device->device(), rchitCode);
    VkShaderModule rmissModule = vox::createShaderModule(m_device->device(), rmissCode);
    VkShaderModule rintModule = vox::createShaderModule(m_device->device(), rintCode);
    
    // Shader stages
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages(4);
    
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].pNext = nullptr;
    shaderStages[0].flags = 0;
    shaderStages[0].stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    shaderStages[0].module = rgenModule;
    shaderStages[0].pName = "main";
    shaderStages[0].pSpecializationInfo = nullptr;
    
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].pNext = nullptr;
    shaderStages[1].flags = 0;
    shaderStages[1].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    shaderStages[1].module = rchitModule;
    shaderStages[1].pName = "main";
    shaderStages[1].pSpecializationInfo = nullptr;
    
    shaderStages[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[2].pNext = nullptr;
    shaderStages[2].flags = 0;
    shaderStages[2].stage = VK_SHADER_STAGE_MISS_BIT_KHR;
    shaderStages[2].module = rmissModule;
    shaderStages[2].pName = "main";
    shaderStages[2].pSpecializationInfo = nullptr;
    
    shaderStages[3].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[3].pNext = nullptr;
    shaderStages[3].flags = 0;
    shaderStages[3].stage = VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
    shaderStages[3].module = rintModule;
    shaderStages[3].pName = "main";
    shaderStages[3].pSpecializationInfo = nullptr;
    
    // Shader groups: raygen, miss, hit (chit + rint)
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups(3);
    
    // Group 0: raygen
    shaderGroups[0].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    shaderGroups[0].pNext = nullptr;
    shaderGroups[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    shaderGroups[0].generalShader = 0;
    shaderGroups[0].closestHitShader = VK_SHADER_UNUSED_KHR;
    shaderGroups[0].anyHitShader = VK_SHADER_UNUSED_KHR;
    shaderGroups[0].intersectionShader = VK_SHADER_UNUSED_KHR;
    
    // Group 1: miss
    shaderGroups[1].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    shaderGroups[1].pNext = nullptr;
    shaderGroups[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    shaderGroups[1].generalShader = 2;
    shaderGroups[1].closestHitShader = VK_SHADER_UNUSED_KHR;
    shaderGroups[1].anyHitShader = VK_SHADER_UNUSED_KHR;
    shaderGroups[1].intersectionShader = VK_SHADER_UNUSED_KHR;
    
    // Group 2: hit (closest hit + intersection for AABB)
    shaderGroups[2].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
    shaderGroups[2].pNext = nullptr;
    shaderGroups[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
    shaderGroups[2].generalShader = VK_SHADER_UNUSED_KHR;
    shaderGroups[2].closestHitShader = 1;
    shaderGroups[2].anyHitShader = VK_SHADER_UNUSED_KHR;
    shaderGroups[2].intersectionShader = 3;
    
    // Create descriptor set with 4 bindings: storage image, nodes, colors, accel struct
    m_descriptorSet = std::make_unique<DescriptorSet>(m_device);
    std::vector<VkDescriptorSetLayoutBinding> bindings(4);

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    bindings[0].pImmutableSamplers = nullptr;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    bindings[1].pImmutableSamplers = nullptr;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    bindings[2].pImmutableSamplers = nullptr;

    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    bindings[3].pImmutableSamplers = nullptr;

    if (!m_descriptorSet->init(bindings)) {
        std::cerr << "Failed to create RTX descriptor set\n";
        vkDestroyShaderModule(m_device->device(), rgenModule, nullptr);
        vkDestroyShaderModule(m_device->device(), rchitModule, nullptr);
        vkDestroyShaderModule(m_device->device(), rmissModule, nullptr);
        vkDestroyShaderModule(m_device->device(), rintModule, nullptr);
        return false;
    }
    
    // Push constants
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    pushRange.offset = 0;
    pushRange.size = sizeof(float) * 6; // time, debugMask, distance, yaw, pitch, fov
    
    // Pipeline layout
    VkDescriptorSetLayout descLayout = m_descriptorSet->layout();
    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &descLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pushRange;
    
    if (vkCreatePipelineLayout(m_device->device(), &plci, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        std::cerr << "Failed to create RT pipeline layout\n";
        vkDestroyShaderModule(m_device->device(), rgenModule, nullptr);
        vkDestroyShaderModule(m_device->device(), rchitModule, nullptr);
        vkDestroyShaderModule(m_device->device(), rmissModule, nullptr);
        vkDestroyShaderModule(m_device->device(), rintModule, nullptr);
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
    rtpci.layout = m_pipelineLayout;
    
    if (m_device->createRayTracingPipelinesKHR(m_device->device(), VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rtpci, nullptr, &m_pipeline) != VK_SUCCESS) {
        std::cerr << "Failed to create RT pipeline\n";
        vkDestroyPipelineLayout(m_device->device(), m_pipelineLayout, nullptr);
        vkDestroyShaderModule(m_device->device(), rgenModule, nullptr);
        vkDestroyShaderModule(m_device->device(), rchitModule, nullptr);
        vkDestroyShaderModule(m_device->device(), rmissModule, nullptr);
        vkDestroyShaderModule(m_device->device(), rintModule, nullptr);
        return false;
    }
    
    // Create SBT from pipeline
    m_sbt = std::make_unique<ShaderBindingTable>(m_device);
    if (!m_sbt->buildFromPipeline(m_pipeline)) {
        std::cerr << "Failed to build shader binding table\n";
        vkDestroyPipeline(m_device->device(), m_pipeline, nullptr);
        vkDestroyPipelineLayout(m_device->device(), m_pipelineLayout, nullptr);
        vkDestroyShaderModule(m_device->device(), rgenModule, nullptr);
        vkDestroyShaderModule(m_device->device(), rchitModule, nullptr);
        vkDestroyShaderModule(m_device->device(), rmissModule, nullptr);
        vkDestroyShaderModule(m_device->device(), rintModule, nullptr);
        return false;
    }

    vkDestroyShaderModule(m_device->device(), rgenModule, nullptr);
    vkDestroyShaderModule(m_device->device(), rchitModule, nullptr);
    vkDestroyShaderModule(m_device->device(), rmissModule, nullptr);
    vkDestroyShaderModule(m_device->device(), rintModule, nullptr);
    
    std::cout << "✓ Ray tracing pipeline created\n";
    return true;
}

void RayTracingPipeline::recordRenderCommands(VkCommandBuffer cmdBuf,
                                              const Scene& scene,
                                              const Camera& camera,
                                              VkImage targetImage,
                                              const VkExtent2D& extent) {
    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipeline);
    
    if (m_descriptorSet) {
        VkDescriptorSet descSet = m_descriptorSet->set();
        vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipelineLayout,
                                0, 1, &descSet, 0, nullptr);
    }

    // Push constants: time, debugMask, distance, yaw, pitch, fov (6 floats)
    struct { float time; float debugMask; float distance; float yaw; float pitch; float fov; } pc{};
    pc.time = 0.f; // Will be set by renderer
    pc.debugMask = 0.f; // Will be set by renderer
    pc.distance = 0.f; // Will be set by renderer
    pc.yaw = 0.f; // Will be set by renderer
    pc.pitch = 0.f; // Will be set by renderer
    pc.fov = 60.f; // Will be set by renderer

    vkCmdPushConstants(cmdBuf, m_pipelineLayout, 
                      VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 
                      0, sizeof(pc), &pc);

    if (m_sbt) {
        const auto& rgen = m_sbt->rgenRegion();
        const auto& miss = m_sbt->missRegion();
        const auto& hit = m_sbt->hitRegion();
        const auto& call = m_sbt->callRegion();
        m_device->cmdTraceRaysKHR(cmdBuf, &rgen, &miss, &hit, &call,
                                   extent.width, extent.height, 1);
    }
}

} // namespace vox
