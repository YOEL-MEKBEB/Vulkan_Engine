#include "vk_manager.h"
#include "vk_types.h"
VkResourceManager::VkResourceManager(VkDevice& device, VmaAllocator& allocator, VkQueue& graphicsQueue, VkFence& fence):
_device(device),
_graphicsQueue(graphicsQueue),
_fence(fence){
  
}
AllocatedBuffer VkResourceManager::create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage){
  
}
void VkResourceManager::destroy_buffer(const AllocatedBuffer& buffer){
  
}

void VkResourceManager::immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function){
  
}
