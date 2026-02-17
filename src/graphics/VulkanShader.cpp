#include "vox/graphics/VulkanShader.h"
#include "vox/graphics/VulkanDevice.h"
#include <fstream>
#include <stdexcept>

namespace vox {

VulkanShader::VulkanShader(VulkanDevice* device, const std::string& path) : m_device(device) {
    auto code = loadSpv(path);
    if (code.empty()) {
        throw std::runtime_error("Failed to load shader: " + path);
    }

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    if (vkCreateShaderModule(device->device(), &createInfo, nullptr, &m_module) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module: " + path);
    }
}

VulkanShader::~VulkanShader() {
    if (m_module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(m_device->device(), m_module, nullptr);
    }
}

std::vector<char> VulkanShader::loadSpv(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) return {};
    
    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    
    return buffer;
}

} // namespace vox
