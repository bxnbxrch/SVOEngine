#include "vox/render/SceneRenderer.h"
#include "vox/graphics/VulkanDevice.h"
#include "vox/graphics/Pipeline.h"
#include "vox/scene/Camera.h"
#include "vox/scene/Scene.h"
#include <iostream>

namespace vox {

SceneRenderer::SceneRenderer(VulkanDevice* device, Scene* scene)
    : m_device(device), m_scene(scene) {
    m_camera = std::make_unique<Camera>();
}

SceneRenderer::~SceneRenderer() {
    if (m_device->device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device->device());
    }
    if (m_renderDoneSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(m_device->device(), m_renderDoneSemaphore, nullptr);
    }
    if (m_imgAvailSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(m_device->device(), m_imgAvailSemaphore, nullptr);
    }
    if (m_frameTimeline != VK_NULL_HANDLE) {
        vkDestroySemaphore(m_device->device(), m_frameTimeline, nullptr);
    }
    if (m_cmdPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device->device(), m_cmdPool, nullptr);
    }
}

bool SceneRenderer::init(VkSurfaceKHR surface, VkExtent2D extent) {
    m_extent = extent;
    
    if (!createCommandResources()) return false;
    if (!createSyncResources()) return false;
    if (!selectPipeline()) return false;
    if (!m_pipeline->init()) return false;

    std::cout << "SceneRenderer initialized\n";
    return true;
}

void SceneRenderer::drawFrame(const Camera& camera, VkImage swapImage, const VkExtent2D& extent) {
    // Get next image
    uint32_t imageIndex;
    vkAcquireNextImageKHR(m_device->device(),  VK_NULL_HANDLE, UINT64_MAX,
                          m_imgAvailSemaphore, VK_NULL_HANDLE, &imageIndex);

    if (m_frameTimeline != VK_NULL_HANDLE && imageIndex < m_cmdBufferValues.size()) {
        uint64_t waitValue = m_cmdBufferValues[imageIndex];
        if (waitValue > 0) {
            VkSemaphoreWaitInfo waitInfo{};
            waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
            waitInfo.semaphoreCount = 1;
            waitInfo.pSemaphores = &m_frameTimeline;
            waitInfo.pValues = &waitValue;
            vkWaitSemaphores(m_device->device(), &waitInfo, UINT64_MAX);
        }
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(m_cmdBuffers[imageIndex], &beginInfo);

    m_pipeline->recordRenderCommands(m_cmdBuffers[imageIndex], *m_scene, *m_camera, swapImage, extent);

    vkEndCommandBuffer(m_cmdBuffers[imageIndex]);

    VkSemaphoreSubmitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitInfo.semaphore = m_imgAvailSemaphore;
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    waitInfo.deviceIndex = 0;

    VkCommandBufferSubmitInfo cmdInfo{};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = m_cmdBuffers[imageIndex];

    VkSemaphoreSubmitInfo signalInfo{};
    signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalInfo.semaphore = m_renderDoneSemaphore;
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    signalInfo.deviceIndex = 0;

    VkSemaphoreSubmitInfo timelineSignal{};
    timelineSignal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    timelineSignal.semaphore = m_frameTimeline;
    timelineSignal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    timelineSignal.deviceIndex = 0;
    timelineSignal.value = ++m_frameValue;

    VkSemaphoreSubmitInfo signalInfos[] = { signalInfo, timelineSignal };

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.waitSemaphoreInfoCount = 1;
    submitInfo.pWaitSemaphoreInfos = &waitInfo;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdInfo;
    submitInfo.signalSemaphoreInfoCount = 2;
    submitInfo.pSignalSemaphoreInfos = signalInfos;

    vkQueueSubmit2(m_device->graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);

    if (imageIndex < m_cmdBufferValues.size()) {
        m_cmdBufferValues[imageIndex] = m_frameValue;
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_renderDoneSemaphore;
    // Present code would go here with swapchain
}

void SceneRenderer::recreateSwapchain(VkExtent2D newExtent) {
    m_extent = newExtent;
    vkDeviceWaitIdle(m_device->device());
    // Swapchain recreation logic
}

void SceneRenderer::onDistanceAdjust(float delta) const {
    m_camera->adjustDistance(delta);
}

void SceneRenderer::onYawAdjust(float delta) const {
    m_camera->adjustYaw(delta);
}

void SceneRenderer::onPitchAdjust(float delta) const {
    m_camera->adjustPitch(delta);
}

void SceneRenderer::onPauseToggle() const {
    m_camera->togglePause();
}

void SceneRenderer::onGridToggle() {
    m_showGridOverlay = !m_showGridOverlay;
}

bool SceneRenderer::createCommandResources() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = m_device->graphicsQueueFamily();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(m_device->device(), &poolInfo, nullptr, &m_cmdPool) != VK_SUCCESS) {
        std::cerr << "Failed to create command pool\n";
        return false;
    }

    m_cmdBuffers.resize(3); // Triple buffering
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_cmdPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = m_cmdBuffers.size();

    if (vkAllocateCommandBuffers(m_device->device(), &allocInfo, m_cmdBuffers.data()) != VK_SUCCESS) {
        std::cerr << "Failed to allocate command buffers\n";
        return false;
    }

    return true;
}

bool SceneRenderer::createSyncResources() {
    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    if (vkCreateSemaphore(m_device->device(), &semInfo, nullptr, &m_imgAvailSemaphore) != VK_SUCCESS ||
        vkCreateSemaphore(m_device->device(), &semInfo, nullptr, &m_renderDoneSemaphore) != VK_SUCCESS) {
        std::cerr << "Failed to create semaphores\n";
        return false;
    }

    VkSemaphoreTypeCreateInfo timelineInfo{};
    timelineInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineInfo.initialValue = 0;

    semInfo.pNext = &timelineInfo;
    if (vkCreateSemaphore(m_device->device(), &semInfo, nullptr, &m_frameTimeline) != VK_SUCCESS) {
        std::cerr << "Failed to create timeline semaphore\n";
        return false;
    }
    semInfo.pNext = nullptr;

    m_cmdBufferValues.assign(m_cmdBuffers.size(), 0);

    return true;
}

} // namespace vox
