#pragma once
#include "vk_types.h"
#include "vulkan/vulkan_core.h"

namespace vkutil{
  bool load_shader_module(const char* filepath, VkDevice device, VkShaderModule* outShaderModule);
};
