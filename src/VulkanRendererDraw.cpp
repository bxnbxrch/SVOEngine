#include "vox/VulkanRenderer.h"
#include "VulkanRendererCommon.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <glm/glm.hpp>
#include <iostream>

namespace vox {

void VulkanRenderer::toggleGridOverlay() {
    m_showSvoOverlay = !m_showSvoOverlay;
    std::cout << "SVO overlay " << (m_showSvoOverlay ? "ON" : "OFF") << "\n";
}

void VulkanRenderer::toggleDebugLighting() {
    m_debugMode = (m_debugMode == 1) ? 0 : 1;
    std::cout << "Debug lighting " << (m_debugMode == 1 ? "ON" : "OFF") << "\n";
}

void VulkanRenderer::toggleGUI() {
    m_guiVisible = !m_guiVisible;
    std::cout << "GUI " << (m_guiVisible ? "ON" : "OFF") << "\n";
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

    if (m_frameTimeline != VK_NULL_HANDLE && imgIndex < m_cmdBufferValues.size()) {
        uint64_t waitValue = m_cmdBufferValues[imgIndex];
        if (waitValue > 0) {
            VkSemaphoreWaitInfo waitInfo{};
            waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
            waitInfo.semaphoreCount = 1;
            waitInfo.pSemaphores = &m_frameTimeline;
            waitInfo.pValues = &waitValue;
            vkWaitSemaphores(m_device, &waitInfo, UINT64_MAX);
        }
    }

    if (m_imguiInitialized) {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        if (m_guiVisible) {
            ImGui::Begin("Debug & Lighting");
        ImGui::Checkbox("SVO overlay", &m_showSvoOverlay);
        ImGui::Separator();
        
        ImGui::SliderFloat("Resolution scale", &m_resolutionScale, 0.25f, 1.0f);
        ImGui::Checkbox("Temporal accumulation", &m_temporalEnabled);
        ImGui::Separator();
        
        ImGui::Text("Camera Mode: %s", m_freeFlyCameraMode ? "FREE-FLY" : "ORBIT");
        ImGui::Text("Press 'V' to toggle camera mode");
        if (m_freeFlyCameraMode) {
            ImGui::Text("WASD = move, QE = up/down, Mouse = look");
        }
        ImGui::Separator();

        const char* debugModes[] = { "Normal", "Lighting", "Albedo", "Normals", "Emissive" };
        ImGui::Combo("Debug mode", &m_debugMode, debugModes, 5);

        ImGui::ColorEdit3("Background", &m_shaderParams.bgColor.x);
        ImGui::SliderFloat("Ambient", &m_shaderParams.params0.x, 0.0f, 1.0f);

        ImGui::SliderFloat3("Key dir", &m_shaderParams.keyDir.x, -1.0f, 1.0f);
        ImGui::SliderFloat("Key weight", &m_shaderParams.keyDir.w, 0.0f, 2.0f);
        ImGui::SliderFloat3("Fill dir", &m_shaderParams.fillDir.x, -1.0f, 1.0f);
        ImGui::SliderFloat("Fill weight", &m_shaderParams.fillDir.w, 0.0f, 2.0f);

        ImGui::SliderFloat("Emissive self", &m_shaderParams.params0.y, 0.0f, 10.0f);
        ImGui::SliderFloat("Emissive direct", &m_shaderParams.params0.z, 0.0f, 10.0f);
        ImGui::SliderFloat("Light atten", &m_shaderParams.params0.w, 0.0f, 0.1f);
        ImGui::SliderFloat("Light atten bias", &m_shaderParams.params1.x, 0.0f, 4.0f);
        ImGui::SliderFloat("Max emissive lights", &m_shaderParams.params1.y, 0.0f, 512.0f);

        ImGui::SliderFloat("DDA epsilon", &m_shaderParams.params1.w, 0.00001f, 0.001f, "%.5f");
        ImGui::SliderFloat("DDA step scale", &m_shaderParams.params2.x, 0.000001f, 0.001f, "%.6f");

        ImGui::Checkbox("Bloom", &m_bloomEnabled);
        ImGui::SliderFloat("Bloom threshold", &m_bloomThreshold, 0.0f, 2.0f);
        ImGui::SliderFloat("Bloom intensity", &m_bloomIntensity, 0.0f, 2.0f);
        ImGui::SliderFloat("Bloom radius", &m_bloomRadius, 1.0f, 6.0f);
        ImGui::End();
        }

        ImGui::Render();
    }

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

    // Dispatch compute shader or trace rays (RTX)
    vkCmdBindPipeline(m_cmdBuffers[imgIndex],
                      m_useRTX ? VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR : VK_PIPELINE_BIND_POINT_COMPUTE,
                      m_rtPipeline);
    DBGPRINT << "drawFrame: pipeline bound\n";
    vkCmdBindDescriptorSets(m_cmdBuffers[imgIndex],
                            m_useRTX ? VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR : VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_rtPipelineLayout,
                            0, 1, &m_rtDescSet, 0, nullptr);
    DBGPRINT << "drawFrame: descriptor sets bound\n";

    // Push time for camera orbit + debug mask + camera params + gridSize
    struct PC { 
        float time; 
        uint32_t debugMask; 
        float distance; 
        float yaw; 
        float pitch; 
        float fov; 
        float gridSize; 
        uint32_t pad;
        glm::vec3 cameraPos;
        float pad2;
        glm::vec3 cameraDir;
        float pad3;
    } pc;

    auto nowTime = std::chrono::high_resolution_clock::now();
    if (m_pauseOrbit) {
        pc.time = m_pausedTime;
    } else {
        if (m_startTime.time_since_epoch().count() == 0) m_startTime = nowTime;
        pc.time = std::chrono::duration<float>(nowTime - m_startTime).count();
    }

    // debugMask: bit0 = show subgrids, bit1 = show root bounds, bit2 = manual control, bit3 = free-fly camera
    uint32_t debugFlags = m_showSvoOverlay ? 1u : 0u;  // Only bit0 for subgrids, no root bounds
    if (m_manualControl) debugFlags |= 4u;
    if (m_freeFlyCameraMode) debugFlags |= 8u;
    pc.debugMask = debugFlags;

    // ensure distance respects safety minimum (for orbit mode)
    const float gridSize = static_cast<float>(m_gridSize);
    float halfDiag = std::sqrt(3.0f) * (gridSize * 0.5f);
    float minDist = halfDiag * 1.2f;
    pc.distance = std::max(m_distance, minDist);
    pc.yaw = m_yaw;
    pc.pitch = m_pitch;
    pc.fov = m_fov;
    pc.gridSize = gridSize;
    pc.pad = 0;
    pc.cameraPos = m_cameraPosition;
    pc.pad2 = 0.0f;
    pc.cameraDir = m_cameraForward;
    pc.pad3 = 0.0f;

    VkShaderStageFlags pushStages = m_useRTX ?
        (VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR) :
        VK_SHADER_STAGE_COMPUTE_BIT;
    vkCmdPushConstants(m_cmdBuffers[imgIndex], m_rtPipelineLayout, pushStages, 0, sizeof(pc), &pc);

    if (m_shaderParamsMemory != VK_NULL_HANDLE) {
        glm::vec3 keyDir = glm::vec3(m_shaderParams.keyDir);
        if (glm::length(keyDir) < 1e-4f) {
            keyDir = glm::vec3(0.6f, 0.8f, 0.4f);
        }
        glm::vec3 fillDir = glm::vec3(m_shaderParams.fillDir);
        if (glm::length(fillDir) < 1e-4f) {
            fillDir = glm::vec3(-0.3f, -0.5f, -0.2f);
        }
        m_shaderParams.keyDir = glm::vec4(glm::normalize(keyDir), m_shaderParams.keyDir.w);
        m_shaderParams.fillDir = glm::vec4(glm::normalize(fillDir), m_shaderParams.fillDir.w);
        m_shaderParams.params1.z = static_cast<float>(m_debugMode);

        void* dst = nullptr;
        vkMapMemory(m_device, m_shaderParamsMemory, 0, sizeof(ShaderParamsCPU), 0, &dst);
        memcpy(dst, &m_shaderParams, sizeof(ShaderParamsCPU));
        vkUnmapMemory(m_device, m_shaderParamsMemory);
    }

    if (m_useRTX) {
        // Ray tracing dispatch
        uint32_t renderWidth = static_cast<uint32_t>(m_extent.width * m_resolutionScale);
        uint32_t renderHeight = static_cast<uint32_t>(m_extent.height * m_resolutionScale);
        renderWidth = std::max(1u, renderWidth);
        renderHeight = std::max(1u, renderHeight);
        
        DBGPRINT << "drawFrame: tracing rays " << renderWidth << "x" << renderHeight << "\n";
        vkCmdTraceRaysKHR(m_cmdBuffers[imgIndex],
                          &m_rgenRegion, &m_missRegion, &m_hitRegion, &m_callRegion,
                          renderWidth, renderHeight, 1);
        DBGPRINT << "drawFrame: ray trace done\n";
    } else {
        // Compute shader dispatch with resolution scaling
        uint32_t renderWidth = static_cast<uint32_t>(m_extent.width * m_resolutionScale);
        uint32_t renderHeight = static_cast<uint32_t>(m_extent.height * m_resolutionScale);
        renderWidth = std::max(1u, renderWidth);
        renderHeight = std::max(1u, renderHeight);
        
        uint32_t groupCountX = (renderWidth + 7) / 8;
        uint32_t groupCountY = (renderHeight + 7) / 8;
        DBGPRINT << "drawFrame: dispatching " << groupCountX << "x" << groupCountY << " groups\n";
        vkCmdDispatch(m_cmdBuffers[imgIndex], groupCountX, groupCountY, 1);
        DBGPRINT << "drawFrame: dispatch done\n";
    }

    VkPipelineStageFlags2 shaderStage = m_useRTX ? VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR
                                                  : VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

    // Barrier: wait for raytrace output before postprocess
    {
        DBGPRINT << "drawFrame: creating memory barrier 1\n";
        VkImageMemoryBarrier2 imb{};
        imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        imb.srcStageMask = shaderStage;
        imb.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        imb.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        imb.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        imb.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
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

        DBGPRINT << "drawFrame: issuing pipeline barrier 1\n";
        vkCmdPipelineBarrier2(m_cmdBuffers[imgIndex], &depInfo);
        DBGPRINT << "drawFrame: barrier 1 issued\n";
    }

    // Bloom postprocess (reads rt image, writes post image)
    if (m_bloomEnabled) {
        vkCmdBindPipeline(m_cmdBuffers[imgIndex], VK_PIPELINE_BIND_POINT_COMPUTE, m_postPipeline);
        vkCmdBindDescriptorSets(m_cmdBuffers[imgIndex], VK_PIPELINE_BIND_POINT_COMPUTE, m_postPipelineLayout,
                                0, 1, &m_postDescSet, 0, nullptr);

        struct BloomPC { float threshold; float intensity; float radius; float padding; } pc;
        pc.threshold = m_bloomThreshold;
        pc.intensity = m_bloomIntensity;
        pc.radius = m_bloomRadius;
        pc.padding = 0.0f;

        vkCmdPushConstants(m_cmdBuffers[imgIndex], m_postPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(pc), &pc);

        uint32_t groupCountX = (m_extent.width + 7) / 8;
        uint32_t groupCountY = (m_extent.height + 7) / 8;
        vkCmdDispatch(m_cmdBuffers[imgIndex], groupCountX, groupCountY, 1);
    }

    // Transition post image to TRANSFER_SRC
    if (m_bloomEnabled) {
        DBGPRINT << "drawFrame: creating memory barrier 1b\n";
        VkImageMemoryBarrier2 imb{};
        imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        imb.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        imb.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        imb.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        imb.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        imb.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        imb.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        imb.image = m_postImage;
        imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imb.subresourceRange.baseMipLevel = 0;
        imb.subresourceRange.levelCount = 1;
        imb.subresourceRange.baseArrayLayer = 0;
        imb.subresourceRange.layerCount = 1;

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &imb;

        vkCmdPipelineBarrier2(m_cmdBuffers[imgIndex], &depInfo);
    }

    // Transition swapchain image to TRANSFER_DST
    {
        DBGPRINT << "drawFrame: creating memory barrier 2\n";
        VkImageMemoryBarrier2 imb{};
        imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        imb.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        imb.srcAccessMask = 0;
        imb.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        imb.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        imb.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        imb.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imb.image = m_swapImages[imgIndex];
        imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imb.subresourceRange.baseMipLevel = 0;
        imb.subresourceRange.levelCount = 1;
        imb.subresourceRange.baseArrayLayer = 0;
        imb.subresourceRange.layerCount = 1;

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &imb;

        DBGPRINT << "drawFrame: issuing pipeline barrier 2\n";
        vkCmdPipelineBarrier2(m_cmdBuffers[imgIndex], &depInfo);
        DBGPRINT << "drawFrame: barrier 2 issued\n";
    }

    // Copy raytrace output to swapchain image (native BGRA format, no conversion needed)
    {
        DBGPRINT << "drawFrame: setting up image copy region\n";

        VkImageCopy region{};
        region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.srcSubresource.mipLevel = 0;
        region.srcSubresource.baseArrayLayer = 0;
        region.srcSubresource.layerCount = 1;
        region.srcOffset = {0, 0, 0};

        region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.dstSubresource.mipLevel = 0;
        region.dstSubresource.baseArrayLayer = 0;
        region.dstSubresource.layerCount = 1;
        region.dstOffset = {0, 0, 0};

        region.extent = {m_extent.width, m_extent.height, 1};

        DBGPRINT << "drawFrame: issuing copy image command\n";
        std::cout.flush();
        VkImage srcImage = m_bloomEnabled ? m_postImage : m_rtImage;
        VkImageLayout srcLayout = m_bloomEnabled ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                                                 : VK_IMAGE_LAYOUT_GENERAL;
        vkCmdCopyImage(m_cmdBuffers[imgIndex], srcImage, srcLayout,
                       m_swapImages[imgIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        DBGPRINT << "drawFrame: copy issued\n";
        std::cout.flush();
    }

    // Transition swapchain image to COLOR_ATTACHMENT_OPTIMAL for ImGui
    {
        VkImageMemoryBarrier2 imb{};
        imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        imb.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        imb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        imb.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        imb.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        imb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imb.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        imb.image = m_swapImages[imgIndex];
        imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imb.subresourceRange.baseMipLevel = 0;
        imb.subresourceRange.levelCount = 1;
        imb.subresourceRange.baseArrayLayer = 0;
        imb.subresourceRange.layerCount = 1;

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &imb;

        vkCmdPipelineBarrier2(m_cmdBuffers[imgIndex], &depInfo);
    }

    if (m_imguiInitialized) {
        VkRenderingAttachmentInfo colorAttach{};
        colorAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttach.imageView = m_imageViews[imgIndex];
        colorAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea.offset = {0, 0};
        renderingInfo.renderArea.extent = m_extent;
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttach;

        vkCmdBeginRendering(m_cmdBuffers[imgIndex], &renderingInfo);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_cmdBuffers[imgIndex]);
        vkCmdEndRendering(m_cmdBuffers[imgIndex]);
    }

    // Transition swapchain image to PRESENT_SRC
    {
        DBGPRINT << "drawFrame: creating memory barrier 4\n";
        VkImageMemoryBarrier2 imb{};
        imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        imb.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        imb.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        imb.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        imb.dstAccessMask = 0;
        imb.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        imb.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        imb.image = m_swapImages[imgIndex];
        imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imb.subresourceRange.baseMipLevel = 0;
        imb.subresourceRange.levelCount = 1;
        imb.subresourceRange.baseArrayLayer = 0;
        imb.subresourceRange.layerCount = 1;

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &imb;

        DBGPRINT << "drawFrame: issuing pipeline barrier 4\n";
        vkCmdPipelineBarrier2(m_cmdBuffers[imgIndex], &depInfo);
        DBGPRINT << "drawFrame: barrier 4 issued\n";
    }

    // Transition post image back to GENERAL for next frame
    if (m_bloomEnabled) {
        DBGPRINT << "drawFrame: creating memory barrier 5\n";
        VkImageMemoryBarrier2 imb{};
        imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        imb.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        imb.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        imb.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        imb.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        imb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        imb.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        imb.image = m_postImage;
        imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imb.subresourceRange.baseMipLevel = 0;
        imb.subresourceRange.levelCount = 1;
        imb.subresourceRange.baseArrayLayer = 0;
        imb.subresourceRange.layerCount = 1;

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &imb;

        DBGPRINT << "drawFrame: issuing pipeline barrier 5\n";
        vkCmdPipelineBarrier2(m_cmdBuffers[imgIndex], &depInfo);
        DBGPRINT << "drawFrame: barrier 5 issued\n";
    }

    DBGPRINT << "drawFrame: ending command buffer\n";
    vkEndCommandBuffer(m_cmdBuffers[imgIndex]);
    DBGPRINT << "drawFrame: command buffer ended\n";

    // Submit command buffer (synchronization2)
    DBGPRINT << "drawFrame: creating submit info\n";
    VkSemaphoreSubmitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitInfo.semaphore = m_imgAvail;
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    waitInfo.deviceIndex = 0;

    VkCommandBufferSubmitInfo cmdInfo{};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = m_cmdBuffers[imgIndex];

    VkSemaphoreSubmitInfo signalInfo{};
    signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalInfo.semaphore = m_renderDone;
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    signalInfo.deviceIndex = 0;

    VkSemaphoreSubmitInfo timelineSignal{};
    timelineSignal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    timelineSignal.semaphore = m_frameTimeline;
    timelineSignal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    timelineSignal.deviceIndex = 0;
    timelineSignal.value = ++m_frameValue;

    VkSemaphoreSubmitInfo signalInfos[] = { signalInfo, timelineSignal };

    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &waitInfo;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;
    submit.signalSemaphoreInfoCount = 2;
    submit.pSignalSemaphoreInfos = signalInfos;

    DBGPRINT << "drawFrame: submitting to queue\n";
    vkQueueSubmit2(m_graphicsQueue, 1, &submit, VK_NULL_HANDLE);
    DBGPRINT << "drawFrame: queue submit done\n";

    if (imgIndex < m_cmdBufferValues.size()) {
        m_cmdBufferValues[imgIndex] = m_frameValue;
    }

    // Present
    DBGPRINT << "drawFrame: creating present info\n";
    VkSemaphore presentWait = m_renderDone;
    VkPresentInfoKHR pres{};
    pres.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pres.waitSemaphoreCount = 1;
    pres.pWaitSemaphores = &presentWait;
    pres.swapchainCount = 1;
    pres.pSwapchains = &m_swapchain;
    pres.pImageIndices = &imgIndex;
    DBGPRINT << "drawFrame: presenting to queue\n";
    vkQueuePresentKHR(m_graphicsQueue, &pres);
    DBGPRINT << "drawFrame: present done\n";

    // Removed vkQueueWaitIdle - unnecessary synchronization that kills performance
    // Semaphores already handle proper GPU/CPU synchronization

    // --- FPS counter: print to console every second (always enabled) ---
    static uint32_t frameCount = 0;
    static auto lastFpsTime = std::chrono::high_resolution_clock::now();
    frameCount++;
    auto nowFps = std::chrono::high_resolution_clock::now();
    float elapsed = std::chrono::duration<float>(nowFps - lastFpsTime).count();
    if (elapsed >= 1.0f) {
        // compute camera pos (matches shader logic)
        const float GRID_SIZE = static_cast<float>(m_gridSize);
        glm::vec3 camPos;
        
        if (m_freeFlyCameraMode) {
            camPos = m_cameraPosition;
        } else {
            const glm::vec3 orbitCenter = glm::vec3(GRID_SIZE * 0.5f);
            const float camHeight = 120.0f;
            float halfDiag = std::sqrt(3.0f) * (GRID_SIZE * 0.5f);
            float desiredDist = halfDiag * 1.2f;
            float minOrbitRadius = std::sqrt(std::max(0.0f, desiredDist * desiredDist - camHeight * camHeight));
            float orbitRadius = std::max(400.0f, minOrbitRadius);
            float angle = pc.time * 0.5f;
            camPos = orbitCenter + glm::vec3(std::sin(angle) * orbitRadius, camHeight, std::cos(angle) * orbitRadius);
        }

        bool inside = (camPos.x >= 0.0f && camPos.x < GRID_SIZE && camPos.y >= 0.0f && camPos.y < GRID_SIZE && camPos.z >= 0.0f && camPos.z < GRID_SIZE);

        std::cout << "FPS: " << frameCount / elapsed << "  |  " << (m_freeFlyCameraMode ? "FLY" : "ORBIT") << " cam=(" << camPos.x << "," << camPos.y << "," << camPos.z << ")" << "  insideSVO=" << (inside ? "YES" : "NO") << std::endl;
        frameCount = 0;
        lastFpsTime = nowFps;
    }

    DBGPRINT << "drawFrame: complete!\n";
}

void VulkanRenderer::adjustDistance(float delta) {
    m_distance += delta;
    // clamp minimum so camera stays outside SVO
    const float GRID_SIZE = static_cast<float>(m_gridSize);
    float halfDiag = std::sqrt(3.0f) * (GRID_SIZE * 0.5f);
    float minDist = halfDiag * 1.2f;
    if (m_distance < minDist) m_distance = minDist;
    m_manualControl = true;
}

void VulkanRenderer::adjustYaw(float delta) {
    m_yaw += delta;
    // wrap to [0, 2*PI)
    const float twoPi = 6.28318530718f;
    while (m_yaw < 0.0f) m_yaw += twoPi;
    while (m_yaw >= twoPi) m_yaw -= twoPi;
    m_manualControl = true;
    if (!m_pauseOrbit) togglePauseOrbit();
}

void VulkanRenderer::adjustPitch(float delta) {
    m_pitch += delta;
    // Allow full range of rotation
    const float maxPitch = 3.14f; // Allow almost full vertical rotation
    const float minPitch = -3.14f;
    if (m_pitch > maxPitch) m_pitch = maxPitch;
    if (m_pitch < minPitch) m_pitch = minPitch;
    m_manualControl = true;
    if (!m_pauseOrbit) togglePauseOrbit();
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

void VulkanRenderer::toggleCameraMode() {
    m_freeFlyCameraMode = !m_freeFlyCameraMode;
    if (m_freeFlyCameraMode) {
        // Switch to free-fly: initialize position from current orbit position
        const float gridSize = static_cast<float>(m_gridSize);
        glm::vec3 target = glm::vec3(gridSize * 0.5f);
        m_cameraPosition = target + glm::vec3(
            m_distance * std::cos(m_pitch) * std::sin(m_yaw),
            m_distance * std::sin(m_pitch),
            m_distance * std::cos(m_pitch) * std::cos(m_yaw)
        );
        m_freeFlyYaw = m_yaw;
        m_freeFlyPitch = m_pitch;
        
        // Update direction vectors
        m_cameraForward = glm::vec3(
            std::cos(m_freeFlyPitch) * std::sin(m_freeFlyYaw),
            std::sin(m_freeFlyPitch),
            std::cos(m_freeFlyPitch) * std::cos(m_freeFlyYaw)
        );
        m_cameraRight = glm::normalize(glm::cross(m_cameraForward, glm::vec3(0, 1, 0)));
        m_cameraUp = glm::cross(m_cameraRight, m_cameraForward);
        
        std::cout << "Free-fly camera mode enabled (WASD to move, mouse to look)\n";
    } else {
        std::cout << "Orbit camera mode enabled\n";
    }
}

void VulkanRenderer::moveCameraForward(float amount) {
    if (m_freeFlyCameraMode) {
        m_cameraPosition += m_cameraForward * amount;
        // std::cout << "MOVE FWD: " << amount << " -> pos=(" << m_cameraPosition.x << "," << m_cameraPosition.y << "," << m_cameraPosition.z << ")\n";
    }
}

void VulkanRenderer::moveCameraRight(float amount) {
    if (m_freeFlyCameraMode) {
        m_cameraPosition += m_cameraRight * amount;
        // std::cout << "MOVE RIGHT: " << amount << " -> pos=(" << m_cameraPosition.x << "," << m_cameraPosition.y << "," << m_cameraPosition.z << ")\n";
    }
}

void VulkanRenderer::moveCameraUp(float amount) {
    if (m_freeFlyCameraMode) {
        m_cameraPosition += glm::vec3(0, 1, 0) * amount; // World up
        // std::cout << "MOVE UP: " << amount << " -> pos=(" << m_cameraPosition.x << "," << m_cameraPosition.y << "," << m_cameraPosition.z << ")\n";
    }
}

void VulkanRenderer::rotateCameraYaw(float delta) {
    if (m_freeFlyCameraMode) {
        m_freeFlyYaw += delta;
        const float twoPi = 6.28318530718f;
        while (m_freeFlyYaw < 0.0f) m_freeFlyYaw += twoPi;
        while (m_freeFlyYaw >= twoPi) m_freeFlyYaw -= twoPi;
        
        // Update direction vectors
        m_cameraForward = glm::vec3(
            std::cos(m_freeFlyPitch) * std::sin(m_freeFlyYaw),
            std::sin(m_freeFlyPitch),
            std::cos(m_freeFlyPitch) * std::cos(m_freeFlyYaw)
        );
        // Use alternative up vector when looking straight up/down to avoid gimbal lock
        glm::vec3 worldUp = (std::abs(m_cameraForward.y) > 0.99f) ? glm::vec3(0, 0, -1) : glm::vec3(0, 1, 0);
        m_cameraRight = glm::normalize(glm::cross(m_cameraForward, worldUp));
        m_cameraUp = glm::cross(m_cameraRight, m_cameraForward);
        
        // std::cout << "ROTATE YAW: delta=" << delta << " yaw=" << m_freeFlyYaw 
        //           << " fwd=(" << m_cameraForward.x << "," << m_cameraForward.y << "," << m_cameraForward.z << ")\n";
    }
}

void VulkanRenderer::rotateCameraPitch(float delta) {
    if (m_freeFlyCameraMode) {
        m_freeFlyPitch += delta;
        const float maxPitch = 1.57f; // 90 degrees
        const float minPitch = -1.57f;
        if (m_freeFlyPitch > maxPitch) m_freeFlyPitch = maxPitch;
        if (m_freeFlyPitch < minPitch) m_freeFlyPitch = minPitch;
        
        // Update direction vectors
        m_cameraForward = glm::vec3(
            std::cos(m_freeFlyPitch) * std::sin(m_freeFlyYaw),
            std::sin(m_freeFlyPitch),
            std::cos(m_freeFlyPitch) * std::cos(m_freeFlyYaw)
        );
        // Use alternative up vector when looking straight up/down to avoid gimbal lock
        glm::vec3 worldUp = (std::abs(m_cameraForward.y) > 0.99f) ? glm::vec3(0, 0, -1) : glm::vec3(0, 1, 0);
        m_cameraRight = glm::normalize(glm::cross(m_cameraForward, worldUp));
        m_cameraUp = glm::cross(m_cameraRight, m_cameraForward);
        
        // std::cout << "ROTATE PITCH: delta=" << delta << " pitch=" << m_freeFlyPitch 
        //           << " fwd=(" << m_cameraForward.x << "," << m_cameraForward.y << "," << m_cameraForward.z << ")\n";
    }
}

void VulkanRenderer::setCameraTransform(const glm::vec3 &pos, const glm::vec3 &forward) {
    // Set position
    m_cameraPosition = pos;

    // Normalize forward and clamp tiny values
    glm::vec3 f = glm::normalize(forward);
    if (glm::length(f) < 1e-6f) f = glm::vec3(0.0f, 0.0f, -1.0f);
    m_cameraForward = f;

    // Derive yaw/pitch from forward
    m_freeFlyPitch = std::asin(glm::clamp(f.y, -1.0f, 1.0f));
    m_freeFlyYaw = std::atan2(f.x, f.z);

    // Recompute basis
    m_cameraRight = glm::normalize(glm::cross(m_cameraForward, glm::vec3(0,1,0)));
    m_cameraUp = glm::cross(m_cameraRight, m_cameraForward);

    // Enable free-fly mode so shader uses these values
    m_freeFlyCameraMode = true;
    
    std::cout << "Camera initialized: pos=(" << pos.x << "," << pos.y << "," << pos.z 
              << ") fwd=(" << f.x << "," << f.y << "," << f.z << ") FREE-FLY mode ON\n";
}

} // namespace vox
