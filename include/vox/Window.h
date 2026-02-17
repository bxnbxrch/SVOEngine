#pragma once
#include <SDL.h>
#include <string>

namespace vox {

class Window {
public:
    Window(const std::string &title = "vox - Vulkan + SDL2 + GLM", int width = 800, int height = 600);
    ~Window();

    bool init();
    void pollEvents(bool &running);
    bool consumeGridToggle();

    // input queries (edge-triggered)
    int consumeWheelDelta();
    bool consumeZoomIn();
    bool consumeZoomOut();
    bool consumeHeightUp();
    bool consumeHeightDown();
    bool consumePauseToggle();
    bool consumeDebugLightingToggle();
    bool consumeCameraToggle();
    bool consumeGUIToggle();
    
    // Continuous key states (level-triggered)
    bool isKeyW() const { return m_keyW; }
    bool isKeyA() const { return m_keyA; }
    bool isKeyS() const { return m_keyS; }
    bool isKeyD() const { return m_keyD; }
    bool isKeyQ() const { return m_keyQ; }
    bool isKeyE() const { return m_keyE; }
    bool isKeyShift() const { return m_keyShift; }

    // mouse drag (returns true if there was movement while left button down)
    bool consumeMouseDrag(int &outDx, int &outDy);
    
    // relative mouse motion (for FPS-style controls)
    bool consumeMouseMotion(int &outDx, int &outDy);
    void setRelativeMouseMode(bool enabled);

    SDL_Window* handle() const { return m_window; }
    void getSize(int &w, int &h) const;
    bool consumeResized();

private:
    SDL_Window* m_window = nullptr;
    std::string m_title;
    int m_width;
    int m_height;

    // transient key toggle for 'G' press
    bool m_gridTogglePressed = false;

    // mouse wheel and keypress transient state (edge-triggered)
    int m_wheelDelta = 0;
    bool m_zoomInPressed = false;
    bool m_zoomOutPressed = false;
    bool m_heightUpPressed = false;
    bool m_heightDownPressed = false;
    bool m_pausePressed = false;
    bool m_debugLightingPressed = false;
    bool m_cameraTogglePressed = false;
    bool m_guiTogglePressed = false;
    
    // Continuous key states (level-triggered)
    bool m_keyW = false;
    bool m_keyA = false;
    bool m_keyS = false;
    bool m_keyD = false;
    bool m_keyQ = false;
    bool m_keyE = false;
    bool m_keyShift = false;

    // mouse drag state
    bool m_mouseDown = false;
    int m_lastMouseX = 0;
    int m_lastMouseY = 0;
    int m_mouseDx = 0;
    int m_mouseDy = 0;
    
    // relative mouse motion (for FPS controls)
    int m_relativeMouseDx = 0;
    int m_relativeMouseDy = 0;
    
    // window resize flag
    bool m_resized = false;
};

} // namespace vox
