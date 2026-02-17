#include "vox/Window.h"
#include "imgui_impl_sdl2.h"
#include <iostream>

namespace vox {

Window::Window(const std::string &title, int width, int height)
    : m_title(title), m_width(width), m_height(height) {}

Window::~Window() {
    if (m_window) SDL_DestroyWindow(m_window);
    SDL_Quit();
}

bool Window::init() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return false;
    }

    m_window = SDL_CreateWindow(m_title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                m_width, m_height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!m_window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        SDL_Quit();
        return false;
    }

    SDL_ShowWindow(m_window);
    SDL_RaiseWindow(m_window);
    std::cout << "SDL video driver: " << (SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "(none)") << std::endl;
    std::cout << "Window id=" << SDL_GetWindowID(m_window) << " flags=" << SDL_GetWindowFlags(m_window) << std::endl;
    return true;
}

void Window::pollEvents(bool &running) {
    // clear per-frame transient key state (do not clear here — consumeGridToggle will reset)

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        ImGui_ImplSDL2_ProcessEvent(&ev);
        if (ev.type == SDL_QUIT) running = false;
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = false;

        if (ev.type == SDL_KEYDOWN) {
            if (ev.key.keysym.sym == SDLK_g) m_gridTogglePressed = true;
            else if (ev.key.keysym.sym == SDLK_l) m_debugLightingPressed = true;
            else if (ev.key.keysym.sym == SDLK_EQUALS) m_zoomInPressed = true;
            else if (ev.key.keysym.sym == SDLK_MINUS) m_zoomOutPressed = true;
            else if (ev.key.keysym.sym == SDLK_UP) m_heightUpPressed = true;
            else if (ev.key.keysym.sym == SDLK_DOWN) m_heightDownPressed = true;
            else if (ev.key.keysym.sym == SDLK_SPACE) m_pausePressed = true;
            else if (ev.key.keysym.sym == SDLK_v) m_cameraTogglePressed = true;
            else if (ev.key.keysym.sym == SDLK_INSERT) m_guiTogglePressed = true;
            else if (ev.key.keysym.sym == SDLK_w) m_keyW = true;
            else if (ev.key.keysym.sym == SDLK_a) m_keyA = true;
            else if (ev.key.keysym.sym == SDLK_s) m_keyS = true;
            else if (ev.key.keysym.sym == SDLK_d) m_keyD = true;
            else if (ev.key.keysym.sym == SDLK_q) m_keyQ = true;
            else if (ev.key.keysym.sym == SDLK_e) m_keyE = true;
            else if (ev.key.keysym.sym == SDLK_LSHIFT || ev.key.keysym.sym == SDLK_RSHIFT) m_keyShift = true;
        }
        
        if (ev.type == SDL_KEYUP) {
            if (ev.key.keysym.sym == SDLK_w) m_keyW = false;
            else if (ev.key.keysym.sym == SDLK_a) m_keyA = false;
            else if (ev.key.keysym.sym == SDLK_s) m_keyS = false;
            else if (ev.key.keysym.sym == SDLK_d) m_keyD = false;
            else if (ev.key.keysym.sym == SDLK_q) m_keyQ = false;
            else if (ev.key.keysym.sym == SDLK_e) m_keyE = false;
            else if (ev.key.keysym.sym == SDLK_LSHIFT || ev.key.keysym.sym == SDLK_RSHIFT) m_keyShift = false;
        }

        if (ev.type == SDL_MOUSEWHEEL) {
            m_wheelDelta += ev.wheel.y;
        }

        if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
            m_mouseDown = true;
            m_lastMouseX = ev.button.x;
            m_lastMouseY = ev.button.y;
        }
        if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) {
            m_mouseDown = false;
        }
        if (ev.type == SDL_MOUSEMOTION && m_mouseDown) {
            // accumulate relative movement while dragging
            m_mouseDx += ev.motion.xrel;
            m_mouseDy += ev.motion.yrel;
        }
        
        if (ev.type == SDL_MOUSEMOTION) {
            // accumulate relative motion for FPS-style controls
            m_relativeMouseDx += ev.motion.xrel;
            m_relativeMouseDy += ev.motion.yrel;
        }

        if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE) running = false;
        if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_RESIZED) m_resized = true;
    }
}

bool Window::consumeGridToggle() {
    bool v = m_gridTogglePressed;
    m_gridTogglePressed = false;
    return v;
}

int Window::consumeWheelDelta() { int v = m_wheelDelta; m_wheelDelta = 0; return v; }

bool Window::consumeZoomIn() { bool v = m_zoomInPressed; m_zoomInPressed = false; return v; }
bool Window::consumeZoomOut() { bool v = m_zoomOutPressed; m_zoomOutPressed = false; return v; }
bool Window::consumeHeightUp() { bool v = m_heightUpPressed; m_heightUpPressed = false; return v; }
bool Window::consumeHeightDown() { bool v = m_heightDownPressed; m_heightDownPressed = false; return v; }
bool Window::consumePauseToggle() { bool v = m_pausePressed; m_pausePressed = false; return v; }
bool Window::consumeDebugLightingToggle() { bool v = m_debugLightingPressed; m_debugLightingPressed = false; return v; }
bool Window::consumeCameraToggle() { bool v = m_cameraTogglePressed; m_cameraTogglePressed = false; return v; }
bool Window::consumeGUIToggle() { bool v = m_guiTogglePressed; m_guiTogglePressed = false; return v; }

bool Window::consumeMouseDrag(int &outDx, int &outDy) {
    outDx = m_mouseDx;
    outDy = m_mouseDy;
    bool had = (m_mouseDx != 0 || m_mouseDy != 0);
    m_mouseDx = 0;
    m_mouseDy = 0;
    return had;
}

bool Window::consumeMouseMotion(int &outDx, int &outDy) {
    outDx = m_relativeMouseDx;
    outDy = m_relativeMouseDy;
    bool had = (m_relativeMouseDx != 0 || m_relativeMouseDy != 0);
    m_relativeMouseDx = 0;
    m_relativeMouseDy = 0;
    return had;
}

void Window::setRelativeMouseMode(bool enabled) {
    SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE);
}

void Window::getSize(int &w, int &h) const {
    SDL_GetWindowSize(m_window, &w, &h);
}

bool Window::consumeResized() {
    bool v = m_resized;
    m_resized = false;
    return v;
}

} // namespace vox
