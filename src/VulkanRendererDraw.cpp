#include "vox/VulkanRenderer.h"
#include "VulkanRendererCommon.h"
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

    // Push time for camera orbit + debug mask + camera params
    struct PC { float time; uint32_t debugMask; float distance; float yaw; float pitch; float fov; } pc;

    auto nowTime = std::chrono::high_resolution_clock::now();
    if (m_pauseOrbit) {
        pc.time = m_pausedTime;
    } else {
        if (m_startTime.time_since_epoch().count() == 0) m_startTime = nowTime;
        pc.time = std::chrono::duration<float>(nowTime - m_startTime).count();
    }

    // debugMask: bit0 = show subgrids, bit1 = show root bounds, bit2 = manual control
    uint32_t debugFlags = m_showSvoOverlay ? 1u : 0u;  // Only bit0 for subgrids, no root bounds
    if (m_manualControl) debugFlags |= 4u;
    pc.debugMask = debugFlags;

    // ensure distance respects safety minimum
    const float GRID_SIZE = 256.0f;
    float halfDiag = std::sqrt(3.0f) * (GRID_SIZE * 0.5f);
    float minDist = halfDiag * 1.2f;
    pc.distance = std::max(m_distance, minDist);
    pc.yaw = m_yaw;
    pc.pitch = m_pitch;
    pc.fov = m_fov;

    VkShaderStageFlags pushStages = m_useRTX ?
        (VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR) :
        VK_SHADER_STAGE_COMPUTE_BIT;
    vkCmdPushConstants(m_cmdBuffers[imgIndex], m_rtPipelineLayout, pushStages, 0, sizeof(pc), &pc);

    if (m_useRTX) {
        // Ray tracing dispatch
        DBGPRINT << "drawFrame: tracing rays " << m_extent.width << "x" << m_extent.height << "\n";
        vkCmdTraceRaysKHR(m_cmdBuffers[imgIndex],
                          &m_rgenRegion, &m_missRegion, &m_hitRegion, &m_callRegion,
                          m_extent.width, m_extent.height, 1);
        DBGPRINT << "drawFrame: ray trace done\n";
    } else {
        // Compute shader dispatch
        uint32_t groupCountX = (m_extent.width + 7) / 8;
        uint32_t groupCountY = (m_extent.height + 7) / 8;
        DBGPRINT << "drawFrame: dispatching " << groupCountX << "x" << groupCountY << " groups\n";
        vkCmdDispatch(m_cmdBuffers[imgIndex], groupCountX, groupCountY, 1);
        DBGPRINT << "drawFrame: dispatch done\n";
    }

    VkPipelineStageFlags2 shaderStage = m_useRTX ? VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR
                                                  : VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

    // Barrier: wait for compute to finish, transition for transfer
    {
        DBGPRINT << "drawFrame: creating memory barrier 1\n";
        VkImageMemoryBarrier2 imb{};
        imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        imb.srcStageMask = shaderStage;
        imb.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        imb.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        imb.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        imb.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        imb.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
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

    // Transition swapchain image to COLOR_ATTACHMENT_OPTIMAL for dynamic rendering clear
    {
        DBGPRINT << "drawFrame: creating memory barrier 2\n";
        VkImageMemoryBarrier2 imb{};
        imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        imb.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        imb.srcAccessMask = 0;
        imb.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        imb.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        imb.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
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

        DBGPRINT << "drawFrame: issuing pipeline barrier 2\n";
        vkCmdPipelineBarrier2(m_cmdBuffers[imgIndex], &depInfo);
        DBGPRINT << "drawFrame: barrier 2 issued\n";
    }

    // Dynamic rendering clear
    {
        VkRenderingAttachmentInfo colorAttach{};
        colorAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttach.imageView = m_imageViews[imgIndex];
        colorAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttach.clearValue.color = {{0.05f, 0.05f, 0.08f, 1.0f}};

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea.offset = {0, 0};
        renderingInfo.renderArea.extent = m_extent;
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttach;

        vkCmdBeginRendering(m_cmdBuffers[imgIndex], &renderingInfo);
        vkCmdEndRendering(m_cmdBuffers[imgIndex]);
    }

    // Transition swapchain image to TRANSFER_DST
    {
        DBGPRINT << "drawFrame: creating memory barrier 3\n";
        VkImageMemoryBarrier2 imb{};
        imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        imb.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        imb.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        imb.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        imb.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        imb.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
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

        DBGPRINT << "drawFrame: issuing pipeline barrier 3\n";
        vkCmdPipelineBarrier2(m_cmdBuffers[imgIndex], &depInfo);
        DBGPRINT << "drawFrame: barrier 3 issued\n";
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
        vkCmdCopyImage(m_cmdBuffers[imgIndex], m_rtImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       m_swapImages[imgIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        DBGPRINT << "drawFrame: copy issued\n";
        std::cout.flush();
    }

    // Transition swapchain image to PRESENT_SRC
    {
        DBGPRINT << "drawFrame: creating memory barrier 4\n";
        VkImageMemoryBarrier2 imb{};
        imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        imb.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        imb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        imb.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        imb.dstAccessMask = 0;
        imb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
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

    // Transition storage image back to GENERAL for next frame
    {
        DBGPRINT << "drawFrame: creating memory barrier 5\n";
        VkImageMemoryBarrier2 imb{};
        imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        imb.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        imb.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        imb.dstStageMask = shaderStage;
        imb.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        imb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
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

void VulkanRenderer::adjustDistance(float delta) {
    m_distance += delta;
    // clamp minimum so camera stays outside SVO
    const float GRID_SIZE = 256.0f;
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

} // namespace vox
