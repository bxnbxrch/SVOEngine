#include <iostream>
#include <vector>

#include <SDL.h>
#include <SDL_vulkan.h>

#include <vulkan/vulkan.h>

#include "vox/Application.h"

int main() {
    vox::Application app;
    if (!app.init()) return 1;
    return app.run();
}