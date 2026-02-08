#pragma once

#include <vk_types.h>
#include <vk_loader.h>
#include "vk_descriptors.h"
#include "vk_pipelines.h"
#include <vk_initializers.h>
#include <vector>

// struct GPUSceneData;
class VulkanEngine;
struct FrameData;
struct RenderObject;

class Skybox{
public:
  Skybox(VkDevice& device, VkDescriptorSetLayout& layout, VmaAllocator& allocator, AllocatedImage& drawImage, AllocatedImage& depthImage);
  ~Skybox();

  void init_skybox_pipeline();
  void init_rect_to_cube_pipeline();
  void load_cube_map(VulkanEngine* engine);
  void convert_to_cube(VulkanEngine* engine);
  void render_skybox(VulkanEngine* engine, VkCommandBuffer cmd, VkRenderingInfo& renderInfo, FrameData& currentFrame, const GPUSceneData& sceneData);
  void clear();
  
	AllocatedImage _cubeMapTextures;
	AllocatedImage _cubeMapHDRTexture;
	AllocatedImage _irradianceMapTexture;
	AllocatedImage _brdfLUTTexture;
  
private:
  bool is_visible(const RenderObject& obj, const glm::mat4& viewproj);
  
  VkDevice& _device;
  AllocatedImage& _depthImage;
  AllocatedImage& _drawImage;
  VmaAllocator& _allocator;

	AllocatedImage _loadedHDRTexture;
    
  VkSampler _defaultSamplerLinear;  // linear interpolation sampling
	VkSampler _defaultSamplerNearest; // nearest neighbors sampling
	VkSampler _defaultCubeSampler;
	DescriptorAllocatorGrowable _skyboxDescriptorAllocator;

  //unlike the shadowPipeline, the skybox  will use the main descriptor layout
  // from the engine so it will be created in the engine code, not here.
	VkDescriptorSetLayout& _gpuSceneDataDescriptorLayout;

  //initialized in current class.
	VkPipelineLayout _skyboxPipelineLayout;
	VkPipeline _skyboxPipeline;

	VkDescriptorSetLayout _brdfLUTDescriptorLayout;
	VkDescriptorSetLayout _rectToCubeDescriptorLayout;//prefilterMap uses the same layout
	
	// VkDescriptorSetLayout _preFilterDescriptorLayout;

	VkPipelineLayout _brdfLUTPipelineLayout;
	VkPipelineLayout _rectToCubePipelineLayout;
	VkPipelineLayout _preFilterPipelineLayout;
	
	VkDescriptorSet _rectToCubeDescriptor;
	VkDescriptorSet _irradianceDescriptor;
	VkDescriptorSet _brdfLUTDescriptor;
	VkDescriptorSet _preFilterDescriptor;
	
	ComputeEffect _rectToCubeEffect;
	ComputeEffect _irradianceEffect;
	ComputeEffect _preFilterEffect;
	ComputeEffect _brdfLUTEffect;

};
