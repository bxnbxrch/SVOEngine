#define GLM_ENABLE_EXPERIMENTAL
#include "vox/Application.h"
#include "imgui.h"
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
    // Initialize cursor (GUI is visible by default, so show cursor and disable relative mouse)
    SDL_ShowCursor(SDL_ENABLE);
    m_window.setRelativeMouseMode(false);
    
    // Initialize camera to consistent benchmarking view (free-fly transform)
    if (m_renderer) {
        glm::vec3 benchPos(2989.34f, 1144.0f, 1832.01f);
        glm::vec3 benchFwd = glm::normalize(glm::vec3(-0.5f, -0.15f, -0.5f));
        m_renderer->setCameraTransform(benchPos, benchFwd);
    }
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

        // debug lighting toggle
        if (m_window.consumeDebugLightingToggle()) {
            if (m_renderer) m_renderer->toggleDebugLighting();
        }
        
        // GUI toggle
        if (m_window.consumeGUIToggle()) {
            if (m_renderer) {
                m_renderer->toggleGUI();
                // Update cursor visibility and relative mouse mode based on GUI state
                bool guiVisible = m_renderer->isGUIVisible();
                SDL_ShowCursor(guiVisible ? SDL_ENABLE : SDL_DISABLE);
                if (guiVisible) {
                    // When GUI is visible, always show cursor and disable relative mouse
                    m_window.setRelativeMouseMode(false);
                } else if (m_renderer->isFreeFlyMode()) {
                    // When GUI is hidden and in fly mode, enable relative mouse
                    m_window.setRelativeMouseMode(true);
                    // Clear accumulated mouse motion to prevent jump
                    int dummy_dx, dummy_dy;
                    m_window.consumeMouseMotion(dummy_dx, dummy_dy);
                }
            }
        }

        bool uiWantsMouse = false;
        bool uiWantsKeyboard = false;
        bool uiWantsTextInput = false;
        bool guiVisible = m_renderer && m_renderer->isGUIVisible();
        if (ImGui::GetCurrentContext()) {
            ImGuiIO& io = ImGui::GetIO();
            uiWantsMouse = io.WantCaptureMouse;
            uiWantsKeyboard = io.WantCaptureKeyboard;
            uiWantsTextInput = io.WantTextInput;  // Only true when typing in text field
            
            static bool printed = false;
            if (!printed) {
                std::cout << "ImGui flags: WantCaptureMouse=" << uiWantsMouse 
                         << " WantCaptureKeyboard=" << uiWantsKeyboard 
                         << " WantTextInput=" << uiWantsTextInput << "\n";
                printed = true;
            }
        }

        // wheel zoom
        int wheel = m_window.consumeWheelDelta();
        if (!guiVisible && !uiWantsMouse && wheel != 0 && m_renderer) {
            // scroll up -> zoom in (smaller distance)
            m_renderer->adjustDistance(-20.0f * (float)wheel);
        }

        // mouse drag -> orbit control or free-fly look
        int mdx=0, mdy=0;
        if (!guiVisible && !uiWantsMouse && m_window.consumeMouseDrag(mdx, mdy) && m_renderer) {
            const float sens = 0.005f;  // radians per pixel
            m_renderer->adjustYaw(-mdx * sens);    // Orbit mode
            m_renderer->adjustPitch(-mdy * sens);  // Orbit mode
            m_renderer->rotateCameraYaw(-mdx * sens);    // Free-fly mode
            m_renderer->rotateCameraPitch(-mdy * sens);  // Free-fly mode
        }
        
        // In free-fly mode, use relative mouse motion for camera look (FPS-style)
        if (!guiVisible && !uiWantsMouse && m_renderer && m_renderer->isFreeFlyMode()) {
            int mmx=0, mmy=0;
            if (m_window.consumeMouseMotion(mmx, mmy)) {
                const float sens = 0.002f;  // radians per pixel
                m_renderer->rotateCameraYaw(-mmx * sens);
                m_renderer->rotateCameraPitch(mmy * sens);
            }
        }

        // keyboard controls
        if (!uiWantsKeyboard && m_window.consumeZoomIn() && m_renderer) m_renderer->adjustDistance(-20.0f);
        if (!uiWantsKeyboard && m_window.consumeZoomOut() && m_renderer) m_renderer->adjustDistance(20.0f);
        if (!uiWantsKeyboard && m_window.consumeHeightUp() && m_renderer) m_renderer->adjustPitch(0.1f);
        if (!uiWantsKeyboard && m_window.consumeHeightDown() && m_renderer) m_renderer->adjustPitch(-0.1f);
        if (m_window.consumePauseToggle() && m_renderer) m_renderer->togglePauseOrbit();
        if (!uiWantsKeyboard && m_window.consumeCameraToggle() && m_renderer) {
            m_renderer->toggleCameraMode();
            // Enable/disable relative mouse mode based on camera mode and GUI state
            bool guiVisible = m_renderer->isGUIVisible();
            SDL_ShowCursor(guiVisible ? SDL_ENABLE : SDL_DISABLE);
            m_window.setRelativeMouseMode(!guiVisible && m_renderer->isFreeFlyMode());
            // Clear accumulated mouse motion to prevent jump when entering fly mode
            if (!guiVisible && m_renderer->isFreeFlyMode()) {
                int dummy_dx, dummy_dy;
                m_window.consumeMouseMotion(dummy_dx, dummy_dy);
            }
        }
        
        // Free-fly camera continuous movement (only blocked when typing in text field)
        if (!uiWantsTextInput && m_renderer) {
            // Base speed is 10x slower, holding shift gives original speed
            const float baseSpeed = 0.5f; // units per frame (10x slower)
            const float fastSpeed = 5.0f; // units per frame (original speed with shift)
            const float moveSpeed = m_window.isKeyShift() ? fastSpeed : baseSpeed;
            
            static int debugCounter = 0;
            bool anyKey = m_window.isKeyW() || m_window.isKeyA() || m_window.isKeyS() || 
                         m_window.isKeyD() || m_window.isKeyQ() || m_window.isKeyE();
            if (anyKey && debugCounter++ % 60 == 0) {
                std::cout << "Keys: W=" << m_window.isKeyW() << " A=" << m_window.isKeyA() 
                         << " S=" << m_window.isKeyS() << " D=" << m_window.isKeyD() 
                         << " Q=" << m_window.isKeyQ() << " E=" << m_window.isKeyE() << "\n";
            }
            if (m_window.isKeyW()) m_renderer->moveCameraForward(moveSpeed);
            if (m_window.isKeyS()) m_renderer->moveCameraForward(-moveSpeed);
            if (m_window.isKeyD()) m_renderer->moveCameraRight(moveSpeed);
            if (m_window.isKeyA()) m_renderer->moveCameraRight(-moveSpeed);
            if (m_window.isKeyE()) m_renderer->moveCameraUp(moveSpeed);
            if (m_window.isKeyQ()) m_renderer->moveCameraUp(-moveSpeed);
        }
        
        m_renderer->drawFrame();
    }
    return 0;
}

} // namespace vox
