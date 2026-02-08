#pragma once

#include "vulkan/vulkan_core.h"
#include <vulkan/vulkan.h>
#include "vk_types.h"
// #include "vk_manager.h"_
namespace vkutil {

//this function hides a bunch of boiler plate code needed for transferring an image
// in the GPU to a different layout. It transfers image from currentLayout to newLayout.
// It uses barriers so that other asynchronous operation don't touch it during this process.
void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout);

//override function to handle cube map mip map generation
void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout, int layerCount);

//It does what the name suggests. It copies from the source to the destination in the GPU.
// Note that the images don't have to be the same size.
void copy_image_to_image(VkCommandBuffer cmd, VkImage source, VkImage destination, VkExtent2D srcSize, VkExtent2D dstSize);

// The barrier is very similar to the one we have on transition_image, and the blit is similar to
// what we have in copy_image_to_image but with mip levels. In a way, this function combines the two.
// At each loop, we divide the image size by four, transition the mip level we copy from, and
// perform a VkCmdBlit from one mip level to the next.
void generate_mipmaps(VkCommandBuffer cmd, VkImage image, VkExtent2D imageSize, int layerCount);

// moved create_image out of the engine because it's getting annoying to pass around the entire engine
// into other functions. The functions in the engine won't be removed because the entire loader depends
// on them and a refactor of the loader will be required.
// TODO: refactor the vk_loader and engine code to use these functions instead  
// AllocatedImage create_image(VkDevice& device, VmaAllocator& allocator, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, VkImageViewType viewType, bool mipmapped = false);
// AllocatedImage create_image(VkDevice& device, VmaAllocator& allocator, void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, VkImageViewType viewType, bool mipmapped = false);
// void destroy_image(VkDevice& device, VmaAllocator& allocator, const AllocatedImage& img);

}




