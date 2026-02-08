#pragma once
#include "vk_types.h"

namespace vkutil {

    AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
    void destroy_buffer(const AllocatedBuffer& buffer);
    void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);
}
    
