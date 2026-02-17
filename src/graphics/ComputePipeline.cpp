#include "vox/graphics/ComputePipeline.h"
#include "vox/graphics/VulkanDevice.h"
#include "vox/graphics/VulkanShader.h"
#include "vox/graphics/DescriptorSet.h"
#include "vox/graphics/VulkanImage.h"
#include "vox/Shader.h"
#include "vox/scene/Scene.h"
#include "vox/scene/Camera.h"
#include <iostream>
#include <glm/glm.hpp>

namespace vox {

ComputePipeline::ComputePipeline(VulkanDevice* device) : Pipeline(device) {}

ComputePipeline::~ComputePipeline() {}

bool ComputePipeline::init() {
    // Load compute shader
    std::vector<char> compCode = vox::loadSpv("shaders/raytrace.comp.spv");
    if (compCode.empty()) {
        std::cerr << "Failed to load compute shader SPIR-V\n";
        return false;
    }

    VkShaderModule compModule = vox::createShaderModule(m_device->device(), compCode);
    if (compModule == VK_NULL_HANDLE) {
        std::cerr << "Compute shader module creation failed\n";
        return false;
    }

    // Create descriptor set with 3 bindings: storage image, nodes buffer, colors buffer
    m_descriptorSet = std::make_unique<DescriptorSet>(m_device);
    std::vector<VkDescriptorSetLayoutBinding> bindings(3);

    // binding 0: storage image
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[0].pImmutableSamplers = nullptr;

    // binding 1: octree nodes buffer
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].pImmutableSamplers = nullptr;

    // binding 2: octree colors buffer
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[2].pImmutableSamplers = nullptr;

    if (!m_descriptorSet->init(bindings)) {
        std::cerr << "Failed to initialize descriptor set\n";
        vkDestroyShaderModule(m_device->device(), compModule, nullptr);
        return false;
    }

    // Create pipeline layout with push constants
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(float) * 6; // time, debugMask, distance, yaw, pitch, fov

    VkDescriptorSetLayout descLayout = m_descriptorSet->layout();
    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &descLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(m_device->device(), &plci, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        std::cerr << "vkCreatePipelineLayout (compute) failed\n";
        vkDestroyShaderModule(m_device->device(), compModule, nullptr);
        return false;
    }

    // Create compute pipeline
    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = compModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.layout = m_pipelineLayout;
    cpci.stage = stageInfo;

    if (vkCreateComputePipelines(m_device->device(), VK_NULL_HANDLE, 1, &cpci, nullptr, &m_pipeline) != VK_SUCCESS) {
        std::cerr << "vkCreateComputePipelines failed\n";
        vkDestroyPipelineLayout(m_device->device(), m_pipelineLayout, nullptr);
        vkDestroyShaderModule(m_device->device(), compModule, nullptr);
        return false;
    }

    vkDestroyShaderModule(m_device->device(), compModule, nullptr);
    std::cout << "✓ Compute pipeline created\n";
    return true;
}

void ComputePipeline::recordRenderCommands(VkCommandBuffer cmdBuf,
                                           const Scene& scene,
                                           const Camera& camera,
                                           VkImage targetImage,
                                           const VkExtent2D& extent) {
    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    
    if (m_descriptorSet) {
        VkDescriptorSet descSet = m_descriptorSet->set();
        vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout,
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

    vkCmdPushConstants(cmdBuf, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

    // Dispatch compute shader with 8x8 workgroups
    uint32_t groupCountX = (extent.width + 7) / 8;
    uint32_t groupCountY = (extent.height + 7) / 8;
    vkCmdDispatch(cmdBuf, groupCountX, groupCountY, 1);
}

} // namespace vox
