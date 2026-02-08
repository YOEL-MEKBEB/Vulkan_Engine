#include "vk_engine.h"
// #include "vulkan/vulkan_core.h"
#include "shadowPipeline.h"

bool ShadowPipeline::is_visible(const RenderObject& obj, const glm::mat4& viewproj) {
    std::array<glm::vec3, 8> corners {
        glm::vec3 { 1, 1, 1 },
        glm::vec3 { 1, 1, -1 },
        glm::vec3 { 1, -1, 1 },
        glm::vec3 { 1, -1, -1 },
        glm::vec3 { -1, 1, 1 },
        glm::vec3 { -1, 1, -1 },
        glm::vec3 { -1, -1, 1 },
        glm::vec3 { -1, -1, -1 },
    };

    glm::mat4 matrix = viewproj * obj.transform;

    glm::vec3 min = { 1.5, 1.5, 1.5 };
    glm::vec3 max = { -1.5, -1.5, -1.5 };

    for (int c = 0; c < 8; c++) {
        // project each corner into clip space
        glm::vec4 v = matrix * glm::vec4(obj.bounds.origin + (corners[c] * obj.bounds.extents), 1.f);

        // perspective correction
        v.x = v.x / v.w;
        v.y = v.y / v.w;
        v.z = v.z / v.w;

        min = glm::min(glm::vec3 { v.x, v.y, v.z }, min);
        max = glm::max(glm::vec3 { v.x, v.y, v.z }, max);
    }

    // check the clip space box is within the view
    if (min.z > 1.f || max.z < 0.f || min.x > 1.f || max.x < -1.f || min.y > 1.f || max.y < -1.f) {
        return false;
    } else {
        return true;
    }
}

 

ShadowPipeline::ShadowPipeline(VkDevice& device, AllocatedImage& lightDepthImage, VmaAllocator& allocator) :
_device(device),
_lightDepthImage(lightDepthImage),
_allocator(allocator)
{
    fmt::print("initialize shadow began\n");
    VkShaderModule shadowVertexShader;
    if (!vkutil::load_shader_module("shaders/shadow_map.vert.spv", _device, &shadowVertexShader)) {
        fmt::println("Error when building the triangle vertex shader module");
    }

    
    VkPushConstantRange matrixRange{};
    matrixRange.offset = 0;
    matrixRange.size = sizeof(GPUDrawPushConstants);
    matrixRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    DescriptorLayoutBuilder builder;
    builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    builder.add_binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    builder.add_binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    

    {
    	DescriptorLayoutBuilder builder;
    	builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    	_shadowSceneDataDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_VERTEX_BIT );
    }
    // VkDescriptorSetLayout shadowDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_VERTEX_BIT);
    VkDescriptorSetLayout layouts[] = {_shadowSceneDataDescriptorLayout};
    
    VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();
    pipeline_layout_info.pPushConstantRanges = &matrixRange;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pSetLayouts = layouts;
    pipeline_layout_info.setLayoutCount = 1; 

    VkPipelineLayout newLayout;
    VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &newLayout));
    
    _shadowPipelineLayout = newLayout;

    PipelineBuilder pipelineBuilder;
    pipelineBuilder._pipelineLayout = newLayout;

    //sending NULL as the fragment shader to set_shaders will cause seg fault.
    // pipelineBuilder.set_shaders(shadowVertexShader, NULL);

    //manually setting the shaders
    pipelineBuilder._shaderStages.clear();
    pipelineBuilder._shaderStages.push_back(
        vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, shadowVertexShader)
    );

    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_FRONT_BIT, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.set_multisampling_none();

    pipelineBuilder.disable_blending();
    pipelineBuilder.enable_depth_test(true, VK_COMPARE_OP_GREATER_OR_EQUAL);
    // pipelineBuilder.enable_depth_test(true, VK_COMPARE_OP_LESS_OR_EQUAL);
    pipelineBuilder.set_depth_format(_lightDepthImage.imageFormat);
    pipelineBuilder.set_color_attachment_format(VK_FORMAT_UNDEFINED);

    _shadowPipeline = pipelineBuilder.build_pipeline(_device);

    fmt::print("shadow initialization complete\n");

    vkDestroyShaderModule(_device, shadowVertexShader, nullptr);

}

ShadowPipeline::~ShadowPipeline(){
    // clear();    
    //don't call clear here because it will cause a segmentation fault
}

void ShadowPipeline::clear(){
		vkDestroyImageView(_device, _lightDepthImage.imageView, nullptr);
    vmaDestroyImage(_allocator, _lightDepthImage.image, _lightDepthImage.allocation);
    vkDestroyDescriptorSetLayout(_device, _shadowSceneDataDescriptorLayout, nullptr);
    vkDestroyPipeline(_device, _shadowPipeline, nullptr);
    vkDestroyPipelineLayout(_device, _shadowPipelineLayout, nullptr);                                 
}


AllocatedBuffer ShadowPipeline::create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage){
    
    VkBufferCreateInfo bufferInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.pNext = nullptr;
    bufferInfo.size = allocSize;

    bufferInfo.usage = usage;

    VmaAllocationCreateInfo vmaallocInfo = {};
    vmaallocInfo.usage = memoryUsage;
    vmaallocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    
    AllocatedBuffer newBuffer;

    // allocate the buffer
    VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaallocInfo, &newBuffer.buffer, &newBuffer.allocation,
        &newBuffer.info));

    return newBuffer;
    
}

void ShadowPipeline::destroy_buffer(const AllocatedBuffer& buffer)
{
    vmaDestroyBuffer(_allocator, buffer.buffer, buffer.allocation);
}

void ShadowPipeline::render_shadow_map(VkCommandBuffer cmd, FrameData& currentFrame, const DrawContext& lightDrawContext, const GPUSceneData& shadowSceneData){
    
std::vector<uint32_t> light_opaque_draws;
    light_opaque_draws.reserve(lightDrawContext.OpaqueSurfaces.size());

    for (uint32_t i = 0; i < lightDrawContext.OpaqueSurfaces.size(); i++) {
        if(is_visible(lightDrawContext.OpaqueSurfaces[i], shadowSceneData.proj * shadowSceneData.view))
            light_opaque_draws.push_back(i);
    }

    std::sort(light_opaque_draws.begin(), light_opaque_draws.end(), [&](const auto& iA, const auto& iB) {
        const RenderObject& A = lightDrawContext.OpaqueSurfaces[iA];
        const RenderObject& B = lightDrawContext.OpaqueSurfaces[iB];
        if (A.material == B.material) {
            return A.indexBuffer < B.indexBuffer;
        }
        else {
            return A.material < B.material;
        }
    });

    VkExtent2D lightImageExtent = {
         _lightDepthImage.imageExtent.width,
         _lightDepthImage.imageExtent.height
    };
    VkRenderingAttachmentInfo lightDepthAttachment = vkinit::depth_attachment_info(_lightDepthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    VkRenderingInfo shadowRenderInfo = vkinit::rendering_info(lightImageExtent, nullptr, &lightDepthAttachment);
    shadowRenderInfo.colorAttachmentCount = 0; //rendering_info() sets it to one even if nullptr is being passed

    vkCmdBeginRendering(cmd, &shadowRenderInfo);
    
    
    VkViewport viewport = {};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = lightImageExtent.width;
    viewport.height = lightImageExtent.height;
    viewport.minDepth = 0.f;
    viewport.maxDepth = 1.f;

    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = lightImageExtent.width;
    scissor.extent.height = lightImageExtent.height;

    vkCmdSetScissor(cmd, 0, 1, &scissor);

    AllocatedBuffer gpuSceneDataBuffer = create_buffer(sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    currentFrame._deletionQueue.push_function([=, this]() {
        destroy_buffer(gpuSceneDataBuffer);
    });

    // GPUSceneData* sceneUniformData = (GPUSceneData*)gpuSceneDataBuffer.allocation->GetMappedData();
    GPUSceneData* sceneUniformData = (GPUSceneData*)gpuSceneDataBuffer.info.pMappedData;
    *sceneUniformData = shadowSceneData;
    
    VkDescriptorSet globalDescriptor = currentFrame._frameDescriptors.allocate(_device, _shadowSceneDataDescriptorLayout);
    
    DescriptorWriter writer;
    writer.write_buffer(0, gpuSceneDataBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.update_set(_device, globalDescriptor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _shadowPipeline);
    
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,_shadowPipelineLayout, 0,1, &globalDescriptor,0,nullptr );

    //need a new pipeline.

  
     for (const RenderObject& draw : lightDrawContext.OpaqueSurfaces) {

         vkCmdBindIndexBuffer(cmd, draw.indexBuffer,0,VK_INDEX_TYPE_UINT32);

         GPUDrawPushConstants pushConstants;
         pushConstants.vertexBuffer = draw.vertexBufferAddress;
         pushConstants.worldMatrix = draw.transform;
         vkCmdPushConstants(cmd,_shadowPipelineLayout,VK_SHADER_STAGE_VERTEX_BIT,0, sizeof(GPUDrawPushConstants), &pushConstants);

         vkCmdDrawIndexed(cmd,draw.indexCount,1,draw.firstIndex,0,0);
     }
    
     vkCmdEndRendering(cmd);
}
