#pragma once

#include <vk_types.h>
#include <glm/glm.hpp>
#include <vk_loader.h>
#include "vk_descriptors.h"
#include "vk_pipelines.h"
#include <vk_initializers.h>
#include <vector>

// struct GPUSceneData;
// class VulkanEngine;
struct FrameData;
struct RenderObject;

class ShadowPipeline{
public:
  ShadowPipeline(VkDevice& device, AllocatedImage& lightDepthImage, VmaAllocator& allocator);
  ~ShadowPipeline();

  //TODO: can get the command buffer from the FrameData
  void render_shadow_map(VkCommandBuffer cmd, FrameData& currentFrame, const DrawContext& lightDrawContext, const GPUSceneData& shadowSceneData);
	AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
  void destroy_buffer(const AllocatedBuffer& buffer);
  void clear();
  
  
private:
  bool is_visible(const RenderObject& obj, const glm::mat4& viewproj);

  //initialized in current class.
  VkPipelineLayout _shadowPipelineLayout;
  VkPipeline _shadowPipeline;
  VkDescriptorSetLayout _shadowSceneDataDescriptorLayout;

  //comes from the engine
  VkDevice& _device;
  AllocatedImage& _lightDepthImage;
  VmaAllocator& _allocator;

};
