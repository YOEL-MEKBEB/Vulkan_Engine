#pragma once

#include "vulkan/vulkan_core.h"
#include <vulkan/vulkan.h>

namespace vkutil {

//this function hides a bunch of boiler plate code needed for transferring an image
// in the GPU to a different layout. It transfers image from currentLayout to newLayout.
// It uses barriers so that other asynchronous operation don't touch it during this process.
void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout);

//It does what the name suggests. It copies from the source to the destination in the GPU.
// Note that the images don't have to be the same size.
void copy_image_to_image(VkCommandBuffer cmd, VkImage source, VkImage destination, VkExtent2D srcSize, VkExtent2D dstSize);

// The barrier is very similar to the one we have on transition_image, and the blit is similar to
// what we have in copy_image_to_image but with mip levels. In a way, this function combines the two.
// At each loop, we divide the image size by four, transition the mip level we copy from, and
// perform a VkCmdBlit from one mip level to the next.
void generate_mipmaps(VkCommandBuffer cmd, VkImage image, VkExtent2D imageSize);
}




