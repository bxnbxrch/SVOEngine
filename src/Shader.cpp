#include "vox/Shader.h"
#include <fstream>
#include <iostream>

namespace vox {

std::vector<char> loadSpv(const std::string &path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        std::cerr << "loadSpv: failed to open '" << path << "'\n";
        return {};
    }
    size_t size = (size_t)f.tellg();
    std::vector<char> buf(size);
    f.seekg(0);
    f.read(buf.data(), size);
    return buf;
}

VkShaderModule createShaderModule(VkDevice device, const std::vector<char> &code) {
    if (code.empty()) return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo smci{};
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = code.size();
    smci.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &smci, nullptr, &module) != VK_SUCCESS) {
        std::cerr << "createShaderModule: vkCreateShaderModule failed\n";
        return VK_NULL_HANDLE;
    }
    return module;
}

} // namespace vox
