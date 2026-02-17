#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>

namespace vox {

// Load a SPIR-V file into a byte vector. Returns empty vector on failure.
std::vector<char> loadSpv(const std::string &path);

// Create a VkShaderModule from SPIR-V bytes. Returns VK_NULL_HANDLE on failure.
VkShaderModule createShaderModule(VkDevice device, const std::vector<char> &code);

} // namespace vox
