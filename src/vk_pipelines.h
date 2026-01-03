#pragma once
#include "vk_types.h"
#include "vulkan/vulkan_core.h"

namespace vkutil{
  // simple code for loading shader modules. They will be stored in outShaderModule.
  bool load_shader_module(const char* filepath, VkDevice device, VkShaderModule* outShaderModule);
};

// This is a general pipeline builder to create different pipeline easily without
// having to deal with the pipeline boilerplate code. It has some issues where it uses default
// parameters and assumes one type of case. So manual modification of the pipeline might be needed.
// For example set_shaders always assumes that you will have vertex shader and a fragment shader
// together. It doesn't account for the case of having a vertex shader and not having fragment shader.
// Shadow maps for example don't need a fragment shader (not including advanced techniques).
// This will be changed in the future.
class PipelineBuilder{
public:
  std::vector<VkPipelineShaderStageCreateInfo> _shaderStages;
   
  VkPipelineInputAssemblyStateCreateInfo _inputAssembly;
  VkPipelineRasterizationStateCreateInfo _rasterizer;
  VkPipelineColorBlendAttachmentState _colorBlendAttachment;
  VkPipelineMultisampleStateCreateInfo _multisampling;
  VkPipelineLayout _pipelineLayout;
  VkPipelineDepthStencilStateCreateInfo _depthStencil;
  VkPipelineRenderingCreateInfo _renderInfo;
  VkFormat _colorAttachmentformat;

  PipelineBuilder(){clear();}
  void clear();
  VkPipeline build_pipeline(VkDevice device);
  void set_shaders(VkShaderModule vertexShader, VkShaderModule fragmentShader);
  void set_input_topology(VkPrimitiveTopology topology);
  void set_polygon_mode(VkPolygonMode mode);
  void set_cull_mode(VkCullModeFlags cullMode, VkFrontFace fronFace);
  void set_multisampling_none();
  void disable_blending();
  void set_color_attachment_format(VkFormat format);
  void set_depth_format(VkFormat format);
  void disable_depth_test();

  //note that enable_depth_test sets the depth bounds from 0 to 1
  // so in a shadow map there is no need to do the translation transform
  // on the z axis of the clipping space.
  void enable_depth_test(bool depthWriteEnable, VkCompareOp op);
  
  void enable_blending_additive();
  void enable_blending_alphablend();
};
