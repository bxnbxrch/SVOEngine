#pragma once

#include "vox/Window.h"
#include "vox/VulkanRenderer.h"
#include <memory>

namespace vox {

class Application {
public:
    Application();
    ~Application();

    bool init();
    int run();

private:
    Window m_window;
    std::unique_ptr<VulkanRenderer> m_renderer;
};

} // namespace vox
