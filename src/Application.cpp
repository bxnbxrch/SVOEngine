#define GLM_ENABLE_EXPERIMENTAL
#include "vox/Application.h"
#include <glm/glm.hpp>
#include <glm/gtx/string_cast.hpp>
#include <iostream>

namespace vox {

Application::Application() : m_window("vox - Vulkan + SDL2 + GLM", 800, 600) {}
Application::~Application() = default;

bool Application::init() {
    if (!m_window.init()) return false;

    m_renderer = std::make_unique<VulkanRenderer>(m_window.handle());
    if (!m_renderer->init()) return false;

    return true;
}

int Application::run() {
    if (!m_renderer || !m_renderer->valid()) return 1;

    bool running = true;
    while (running) {
        m_window.pollEvents(running);
        
        // Handle window resize
        if (m_window.consumeResized() && m_renderer) {
            m_renderer->recreateSwapchain();
        }

        // grid toggle
        if (m_window.consumeGridToggle()) {
            if (m_renderer) m_renderer->toggleGridOverlay();
        }

        // wheel zoom
        int wheel = m_window.consumeWheelDelta();
        if (wheel != 0 && m_renderer) {
            // scroll up -> zoom in (smaller distance)
            m_renderer->adjustDistance(-20.0f * (float)wheel);
        }

        // mouse drag -> orbit control
        int mdx=0, mdy=0;
        if (m_window.consumeMouseDrag(mdx, mdy) && m_renderer) {
            const float sens = 0.005f;  // radians per pixel
            m_renderer->adjustYaw(-mdx * sens);    // left/right rotates horizontally (inverted)
            m_renderer->adjustPitch(-mdy * sens);  // up/down rotates vertically
        }

        // keyboard controls
        if (m_window.consumeZoomIn() && m_renderer) m_renderer->adjustDistance(-20.0f);
        if (m_window.consumeZoomOut() && m_renderer) m_renderer->adjustDistance(20.0f);
        if (m_window.consumeHeightUp() && m_renderer) m_renderer->adjustPitch(0.1f);
        if (m_window.consumeHeightDown() && m_renderer) m_renderer->adjustPitch(-0.1f);
        if (m_window.consumePauseToggle() && m_renderer) m_renderer->togglePauseOrbit();

        m_renderer->drawFrame();
    }
    return 0;
}

} // namespace vox
