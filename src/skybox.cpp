#include "skybox.h"
#include <stb_image.h>
#include "vk_engine.h"
#include "vk_images.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

Skybox::Skybox(VkDevice& device, VkDescriptorSetLayout& layout, VmaAllocator& allocator, AllocatedImage& drawImage, AllocatedImage& depthImage):
_device(device),
_gpuSceneDataDescriptorLayout(layout),
_allocator(allocator),
_depthImage(depthImage),
_drawImage(drawImage)
{
  
  //equirectangular map to cube map Conversion Descriptor layout
  {
      DescriptorLayoutBuilder builder;
      builder.add_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
      builder.add_binding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
      _rectToCubeDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);
  }

  {
      DescriptorLayoutBuilder builder;
      builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
      _brdfLUTDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);
      
  }
  std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes =
  {
      { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
      { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 }, 
      { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 }, 
      { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
  };
  _skyboxDescriptorAllocator.init(_device, 10, sizes);

  _rectToCubeDescriptor = _skyboxDescriptorAllocator.allocate(_device, _rectToCubeDescriptorLayout);
  _irradianceDescriptor = _skyboxDescriptorAllocator.allocate(_device, _rectToCubeDescriptorLayout);
  _brdfLUTDescriptor = _skyboxDescriptorAllocator.allocate(_device, _brdfLUTDescriptorLayout);

  
  VkSamplerCreateInfo sample = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};

  sample.magFilter = VK_FILTER_NEAREST;
  sample.minFilter = VK_FILTER_NEAREST;

  vkCreateSampler(_device, &sample, nullptr, &_defaultSamplerNearest);

  sample.magFilter = VK_FILTER_LINEAR;
  sample.minFilter = VK_FILTER_LINEAR;
  vkCreateSampler(_device, &sample, nullptr, &_defaultSamplerLinear);
  
  sample.magFilter = VK_FILTER_LINEAR;
  sample.minFilter = VK_FILTER_LINEAR;
  sample.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  sample.minLod = 0.0f;
  sample.maxLod = VK_LOD_CLAMP_NONE; // Allows access to all available mip levels

  //Use CLAMP_TO_EDGE for U, V, and W coordinates to prevent edge seams
  sample.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sample.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sample.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

  vkCreateSampler(_device, &sample, nullptr, &_defaultCubeSampler);

  init_skybox_pipeline();
  init_rect_to_cube_pipeline();
}

Skybox::~Skybox(){}

void Skybox::clear(){
  
    vkDestroyPipeline(_device, _skyboxPipeline, nullptr);
    vkDestroyPipelineLayout(_device, _skyboxPipelineLayout, nullptr);                                 
		vkDestroyPipelineLayout(_device, _rectToCubePipelineLayout, nullptr);
		vkDestroyPipelineLayout(_device, _brdfLUTPipelineLayout, nullptr);
		vkDestroyPipelineLayout(_device, _preFilterPipelineLayout, nullptr);
		vkDestroyPipeline(_device, _rectToCubeEffect.pipeline, nullptr);
		vkDestroyPipeline(_device, _irradianceEffect.pipeline, nullptr);
		vkDestroyPipeline(_device, _preFilterEffect.pipeline, nullptr);
		vkDestroyPipeline(_device, _brdfLUTEffect.pipeline, nullptr);
    vkDestroyDescriptorSetLayout(_device, _rectToCubeDescriptorLayout, nullptr);
    vkDestroyDescriptorSetLayout(_device, _brdfLUTDescriptorLayout, nullptr);
    _skyboxDescriptorAllocator.destroy_pools(_device);
    vkDestroySampler(_device,_defaultSamplerNearest,nullptr);
    vkDestroySampler(_device,_defaultSamplerLinear,nullptr);
    vkDestroySampler(_device, _defaultCubeSampler, nullptr);

    auto destroy_image = [&](AllocatedImage& img) {
        vkDestroyImageView(_device, img.imageView, nullptr);
        vmaDestroyImage(_allocator, img.image, img.allocation);
    };
    destroy_image(_cubeMapTextures);
    destroy_image(_cubeMapHDRTexture);
    destroy_image(_loadedHDRTexture);
    destroy_image(_irradianceMapTexture);
    destroy_image(_brdfLUTTexture);
}

void Skybox::init_skybox_pipeline(){
    fmt::print("initialize skybox began\n");
        
    VkShaderModule skyboxFragShader;
    if (!vkutil::load_shader_module("shaders/skybox.frag.spv", _device, &skyboxFragShader)) {
        fmt::println("Error when building the triangle fragment shader module");
    }

 
    VkShaderModule skyboxVertShader;
    if (!vkutil::load_shader_module("shaders/skybox.vert.spv", _device, &skyboxVertShader)) {
        fmt::println("Error when building the triangle vertex shader module");
    }

    
    VkPushConstantRange matrixRange{};
    matrixRange.offset = 0;
    matrixRange.size = sizeof(GPUDrawPushConstants);
    matrixRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    DescriptorLayoutBuilder builder;
    builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    builder.add_binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    builder.add_binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    builder.add_binding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
   

    // VkDescriptorSetLayout shadowDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_VERTEX_BIT);
    VkDescriptorSetLayout layouts[] = {_gpuSceneDataDescriptorLayout};
    
    VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();
    pipeline_layout_info.pPushConstantRanges = &matrixRange;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pSetLayouts = layouts;
    pipeline_layout_info.setLayoutCount = 1; 

    VkPipelineLayout newLayout;
    VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &newLayout));
    
    _skyboxPipelineLayout = newLayout;

    PipelineBuilder pipelineBuilder;
    pipelineBuilder._pipelineLayout = newLayout;

    //sending NULL as the fragment shader to set_shaders will cause seg fault.
    // pipelineBuilder.set_shaders(shadowVertexShader, NULL);

    //manually setting the shaders
    pipelineBuilder._shaderStages.clear();
    pipelineBuilder._shaderStages.push_back(
        vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, skyboxVertShader)
    );
    pipelineBuilder._shaderStages.push_back(
        vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, skyboxFragShader)
    );

    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.set_multisampling_none();

    pipelineBuilder.disable_blending();
    pipelineBuilder.disable_depth_test();
    pipelineBuilder.set_depth_format(_depthImage.imageFormat);
    pipelineBuilder.set_color_attachment_format(_drawImage.imageFormat);

    _skyboxPipeline = pipelineBuilder.build_pipeline(_device);

    fmt::print("skybox initialization complete\n");

    vkDestroyShaderModule(_device, skyboxVertShader, nullptr);
    vkDestroyShaderModule(_device, skyboxFragShader, nullptr);
  
}

void Skybox::init_rect_to_cube_pipeline(){
  
    /////////rect to cube converter compute layout////////
    //creating the layout info for the pipeline
    VkPipelineLayoutCreateInfo computeLayout{};
    computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    computeLayout.pNext = nullptr;
    computeLayout.pSetLayouts = &_rectToCubeDescriptorLayout;
    computeLayout.setLayoutCount = 1;
    computeLayout.pPushConstantRanges = nullptr;
    computeLayout.pushConstantRangeCount = 0;

    ////////////////////////////////////////////////////////

    ///////////pre filter compute layout///////////////
    VkPushConstantRange pushConstant;
    pushConstant.offset = 0; //the push constant range starts at 0
    pushConstant.size = sizeof(float); //setting the size of the pushConstant
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    VkPipelineLayoutCreateInfo preFilterComputeLayout{};
    preFilterComputeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    preFilterComputeLayout.pNext = nullptr;
    preFilterComputeLayout.pSetLayouts = &_rectToCubeDescriptorLayout;
    preFilterComputeLayout.setLayoutCount = 1;
    preFilterComputeLayout.pPushConstantRanges = &pushConstant;
    preFilterComputeLayout.pushConstantRangeCount = 1;
    //////////////////////////////////////////////////

    ////////////brdf look up texture compute layout///////////    
    VkPipelineLayoutCreateInfo brdfLUTcomputeLayout{};
    brdfLUTcomputeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    brdfLUTcomputeLayout.pNext = nullptr;
    brdfLUTcomputeLayout.pSetLayouts = &_brdfLUTDescriptorLayout;
    brdfLUTcomputeLayout.setLayoutCount = 1;
    brdfLUTcomputeLayout.pPushConstantRanges = nullptr;
    brdfLUTcomputeLayout.pushConstantRangeCount = 0;
    /////////////////////////////////////////////////////////

    VK_CHECK(vkCreatePipelineLayout(_device, &computeLayout, nullptr, &_rectToCubePipelineLayout));
    VK_CHECK(vkCreatePipelineLayout(_device, &preFilterComputeLayout, nullptr, &_preFilterPipelineLayout));
    VK_CHECK(vkCreatePipelineLayout(_device, &brdfLUTcomputeLayout, nullptr, &_brdfLUTPipelineLayout));


    //layout code
    VkShaderModule rectToCubeShader;
    if (!vkutil::load_shader_module("shaders/rect_to_cube.comp.spv", _device, &rectToCubeShader))
    {
        fmt::print("Error when building the compute shader \n");
    }

    VkShaderModule irradianceShader;
    if (!vkutil::load_shader_module("shaders/irradiance.comp.spv", _device, &irradianceShader))
    {
        fmt::print("Error when building the compute shader \n");
    }

    VkShaderModule preFilterShader;
    if (!vkutil::load_shader_module("shaders/pre_filter_conv.comp.spv", _device, &preFilterShader))
    {
        fmt::print("Error when building the compute shader \n");
    }

    VkShaderModule brdfLUTShader;
    if (!vkutil::load_shader_module("shaders/brdfLUT.comp.spv", _device, &brdfLUTShader))
    {
        fmt::print("Error when building the compute shader \n");
    }

    VkPipelineShaderStageCreateInfo stageinfo{};
    stageinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageinfo.pNext = nullptr;
    stageinfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageinfo.module = rectToCubeShader;
    //note here is that we are giving
    // it the name of the function we want
    // the shader to use, which is going to be main().
    stageinfo.pName = "main";

    VkComputePipelineCreateInfo computePipelineCreateInfo{};
    computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computePipelineCreateInfo.pNext = nullptr;
    computePipelineCreateInfo.layout = _rectToCubePipelineLayout;
    computePipelineCreateInfo.stage = stageinfo;


    ComputeEffect toCube;
    toCube.layout = _rectToCubePipelineLayout;
    toCube.name = "toCube";
    toCube.data = {};



    VK_CHECK(vkCreateComputePipelines(_device,VK_NULL_HANDLE,1,&computePipelineCreateInfo, nullptr, &toCube.pipeline));

    computePipelineCreateInfo.stage.module = irradianceShader;
    
    ComputeEffect irradiance;
    irradiance.layout = _rectToCubePipelineLayout;
    irradiance.name = "irradiance";
    irradiance.data = {};
    
    VK_CHECK(vkCreateComputePipelines(_device,VK_NULL_HANDLE,1,&computePipelineCreateInfo, nullptr, &irradiance.pipeline));


    computePipelineCreateInfo.layout = _preFilterPipelineLayout;
    computePipelineCreateInfo.stage.module = preFilterShader;

    ComputeEffect preFilter;
    preFilter.layout = _preFilterPipelineLayout;
    preFilter.name = "preFilter";
    preFilter.data = {};
    // preFilter.data.data1 = metal_rough_factors;

    VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &preFilter.pipeline));

    computePipelineCreateInfo.layout = _brdfLUTPipelineLayout;
    computePipelineCreateInfo.stage.module = brdfLUTShader;

    ComputeEffect brdfLUT;
    brdfLUT.layout = _brdfLUTPipelineLayout;
    brdfLUT.name = "brdfLUT";
    brdfLUT.data = {};

    VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &brdfLUT.pipeline));
    

    vkDestroyShaderModule(_device, rectToCubeShader, nullptr);
    vkDestroyShaderModule(_device, irradianceShader, nullptr);
    vkDestroyShaderModule(_device, preFilterShader, nullptr);
    vkDestroyShaderModule(_device, brdfLUTShader, nullptr);
    _rectToCubeEffect = toCube;
    _irradianceEffect = irradiance;
    _preFilterEffect = preFilter;
    _brdfLUTEffect = brdfLUT;
    
}

void Skybox::load_cube_map(VulkanEngine* engine){
  
    // name of all the textures for the skybox/cubemap
    std::vector<std::string> faces
    {
        "assets/right.jpg",
        "assets/left.jpg",
        "assets/top.jpg",
        "assets/bottom.jpg",
        "assets/front.jpg",
        "assets/back.jpg"
    };
    // _cubeMapTextures.reserve(faces.size());
    std::vector<unsigned char> textures;
    // textures.reserve(faces.size());
    int total_width = 0;
    int total_height = 0;

    int width, height, channel;
    for(int i = 0; i < faces.size(); i++){
        unsigned char* data = stbi_load(faces[i].c_str(), &width, &height, &channel, 4);
        if(data){
            size_t size = width * height * 4;
            textures.insert(textures.end(), data, data + size);
        }else{
            fmt::print("Error loading cube map textures\n");
        }

        stbi_image_free(data);
    }

    //this is technically a hack because I know
    // all the images have the same size
    VkExtent3D size = { 
        static_cast<uint32_t>(width), 
        static_cast<uint32_t>(height), 
        1 
    };
    uint32_t faceWidth = static_cast<uint32_t>(width);
    uint32_t faceHeight = static_cast<uint32_t>(height);

    // VkExtent3D size = {static_cast<uint32_t>(total_width), static_cast<uint32_t>(total_height), 1};
    size_t data_size = textures.size();
    AllocatedBuffer uploadbuffer = engine->create_buffer(data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    memcpy(uploadbuffer.info.pMappedData, textures.data(), data_size);

    AllocatedImage new_image = engine->create_image(size, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_VIEW_TYPE_CUBE, false);


    engine->immediate_submit([&](VkCommandBuffer cmd) {
    		vkutil::transition_image(cmd, new_image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    		VkBufferImageCopy copyRegion = {};
    		copyRegion.bufferOffset = 0;
    		copyRegion.bufferRowLength = 0;
    		copyRegion.bufferImageHeight = 0;

    		copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    		copyRegion.imageSubresource.mipLevel = 0;
    		copyRegion.imageSubresource.baseArrayLayer = 0;
    		copyRegion.imageSubresource.layerCount = 6; //for each face/image on the cube
    		copyRegion.imageExtent = size;

    		// copy the buffer into the image
    		vkCmdCopyBufferToImage(cmd, uploadbuffer.buffer, new_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
    			&copyRegion);

    		vkutil::transition_image(cmd, new_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });


    engine->destroy_buffer(uploadbuffer);

    _cubeMapTextures = new_image;

    textures.clear();
    
    // stbi_set_flip_vertically_on_load(true);
    // float *data = stbi_loadf("assets/rogland_moonlit_night_4k.hdr", &width, &height, &channel, 4);
    // float *data = stbi_loadf("assets/cave_wall_4k.hdr", &width, &height, &channel, 4);
    float *data = stbi_loadf("assets/brown_photostudio_02_4k.hdr", &width, &height, &channel, 4);
    
    if (data)
    {
        size = { 
            static_cast<uint32_t>(width), 
            static_cast<uint32_t>(height), 
            1
        };

        //need to load as a 2D texture for equirectangular projection.
        //The image will be processed and get converted into a cube map but for now I'll set it
        // to _cubeMapHDrTexture.
        AllocatedImage newer_image= engine->create_image(data, size, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_VIEW_TYPE_2D, false);        

        stbi_image_free(data);
        size = { 
            faceWidth, 
            faceHeight, 
            1
        };
       
        _loadedHDRTexture = newer_image;
        VkExtent3D cubemapSize = { 1024, 1024, 1 };

        uint32_t irrSize = 32;
        VkExtent3D irradianceMapSize = {irrSize, irrSize, 1};

        _cubeMapHDRTexture = engine->create_image(cubemapSize, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_VIEW_TYPE_CUBE, true);
        _irradianceMapTexture = engine->create_image(irradianceMapSize, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_VIEW_TYPE_CUBE,false);
        _brdfLUTTexture = engine->create_image(cubemapSize, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_VIEW_TYPE_2D,false);
        
    }
    else
    {
        fmt::print("failed to load hdr\n");        
    }  

       
}

void Skybox::convert_to_cube(VulkanEngine* engine){
  
    /*
    VK_CHECK(vkResetFences(_device, 1, &get_current_frame()._renderFence));
    VkCommandBuffer cmd = get_current_frame()._mainCommandBuffer;
    VK_CHECK(vkResetCommandBuffer(cmd, 0));
    VkCommandBufferBeginInfo cmdInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdInfo));
    */

    engine->immediate_submit([&](VkCommandBuffer cmd) {
        vkutil::transition_image(cmd, _loadedHDRTexture.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
        vkutil::transition_image(cmd, _cubeMapHDRTexture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,6);
        vkutil::transition_image(cmd, _irradianceMapTexture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,6);


        ////////generate the cube map from the hdr texture/////////////
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _rectToCubeEffect.pipeline);

        // bind the descriptor set containing the draw image for the compute pipeline
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _rectToCubePipelineLayout, 0, 1, &_rectToCubeDescriptor, 0, nullptr);

        DescriptorWriter writer;
        writer.write_image(0, _loadedHDRTexture.imageView, _defaultSamplerLinear, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        writer.write_image(1, _cubeMapHDRTexture.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        writer.update_set(_device, _rectToCubeDescriptor);
        writer.clear();

        // execute the compute pipeline dispatch. We are using 16x16 workgroup size so we need to divide by it
        uint32_t faceSize = _cubeMapHDRTexture.imageExtent.width; //the width is equal to the height
        vkCmdDispatch(cmd, std::ceil(faceSize / 16.0), std::ceil(faceSize / 16.0), 6);
        vkutil::transition_image(cmd, _cubeMapHDRTexture.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 6);
        vkutil::generate_mipmaps(cmd, _cubeMapHDRTexture.image, VkExtent2D{ _cubeMapHDRTexture.imageExtent.width, _cubeMapHDRTexture.imageExtent.height }, 6);

        //-----generate_mipmaps already converts it to shader read only.
        // vkutil::transition_image(cmd, _cubeMapHDRTexture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 6);


        vkutil::transition_image(cmd, _cubeMapHDRTexture.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, 6);
        // vkutil::transition_image(cmd, _cubeMapHDRTexture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL. 6);

        ///////////////prefilter map generations//////////////////

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _preFilterEffect.pipeline);

        VkExtent3D cubeMapSize = _cubeMapHDRTexture.imageExtent;
        AllocatedImage preFilteredImage = engine->create_image(cubeMapSize, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_VIEW_TYPE_CUBE, true);
        vkutil::transition_image(cmd, preFilteredImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 6);

        int maxMipLevel = 5;

        //In Vulkan, you cannot update a Descriptor Set that is already recorded in the command buffer.
        // When you update it for Mip 1, you invalidate the command you just recorded for Mip 0. Sadness T.T
        for (int mip = 0; mip < maxMipLevel; mip++) {
            uint32_t mipWidth = faceSize * std::pow(0.5, mip);
            uint32_t mipHeight = faceSize * std::pow(0.5, mip);
            float roughness = (float)mip / (float)(maxMipLevel - 1);

            //Obtained help from LLM.
            VkImageViewCreateInfo viewInfo = vkinit::imageview_create_info(
                preFilteredImage.imageFormat,
                preFilteredImage.image, //this will make sure the image view is assigned to the preFilteredImage.
                VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_VIEW_TYPE_CUBE
            );
            viewInfo.subresourceRange.baseMipLevel = mip; // this image view will be associated to this specific mip level.
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.layerCount = 6;

            VkImageView mipView;
            VK_CHECK(vkCreateImageView(_device, &viewInfo, nullptr, &mipView));

            //must bind a different descriptor for each mip level
            _preFilterDescriptor = _skyboxDescriptorAllocator.allocate(_device, _rectToCubeDescriptorLayout);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _preFilterPipelineLayout, 0, 1, &_preFilterDescriptor, 0, nullptr);

            DescriptorWriter mipWriter;
            mipWriter.write_image(0, _cubeMapHDRTexture.imageView, _defaultSamplerLinear, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            mipWriter.write_image(1, mipView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
            mipWriter.update_set(_device, _preFilterDescriptor);
            mipWriter.clear();

            vkCmdPushConstants(cmd, _preFilterPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(float), &roughness);

            vkCmdDispatch(cmd, std::ceil(mipWidth / 16.0), std::ceil(mipHeight / 16.0), 6);
            engine->_mainDeletionQueue.push_function([=]() {
                vkDestroyImageView(_device, mipView, nullptr);
                });

        }
        AllocatedImage oldImage = _cubeMapHDRTexture;
        engine->_mainDeletionQueue.push_function([=]() {
            engine->destroy_image(oldImage);
        });
        _cubeMapHDRTexture = preFilteredImage;
        vkutil::transition_image(cmd, _cubeMapHDRTexture.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 6);
        ///////////////////////////////////////////////////////////////


        ///////////////generate the brdfLUTTexture////////////////////

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _brdfLUTEffect.pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _brdfLUTPipelineLayout, 0, 1, &_brdfLUTDescriptor, 0, nullptr);
        DescriptorWriter brdfWriter;
        brdfWriter.write_image(0, _brdfLUTTexture.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        brdfWriter.update_set(_device, _brdfLUTDescriptor);
        brdfWriter.clear();

        vkutil::transition_image(cmd, _brdfLUTTexture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        vkCmdDispatch(cmd, std::ceil(faceSize / 16.0), std::ceil(faceSize / 16.0), 1);
        vkutil::transition_image(cmd, _brdfLUTTexture.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        /////////////////////////////////////////////////////////////

        //////generate the irradiance map.///////////////////////////
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _irradianceEffect.pipeline);

        // bind the descriptor set containing the draw image for the compute pipeline
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _rectToCubePipelineLayout, 0, 1, &_irradianceDescriptor, 0, nullptr);

        writer.write_image(0, _cubeMapHDRTexture.imageView, _defaultCubeSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        writer.write_image(1, _irradianceMapTexture.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        writer.update_set(_device, _irradianceDescriptor);
        writer.clear();

        uint32_t irrSize = _irradianceMapTexture.imageExtent.width;
        vkCmdDispatch(cmd, std::ceil(irrSize / 16.0), std::ceil(irrSize / 16.0), 6);
        vkutil::transition_image(cmd, _irradianceMapTexture.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 6);
        ///////////////////////////////////////////////////// c



    });

    /*
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmdSubmitInfo = vkinit::command_buffer_submit_info(cmd);
    // VkSemaphoreSubmitInfo waitSemaphore = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, get_current_frame()._swapchainSemaphore);
    // VkSemaphoreSubmitInfo signalSemaphore = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, get_current_frame()._renderSemaphore);
    VkSubmitInfo2 submit = vkinit::submit_info(&cmdSubmitInfo, nullptr, nullptr);
    VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, get_current_frame()._renderFence));
    */
    
}

void Skybox::render_skybox(VulkanEngine* engine, VkCommandBuffer cmd, VkRenderingInfo& renderInfo, FrameData& currentFrame, const GPUSceneData& sceneData){
  
    //Bind Pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _skyboxPipeline);

    VkDescriptorSet globalDescriptor = currentFrame._frameDescriptors.allocate(_device, _gpuSceneDataDescriptorLayout);
    
    DescriptorWriter writer;
    AllocatedBuffer gpuSceneDataBuffer = engine->create_buffer(sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    // GPUSceneData* sceneUniformData = (GPUSceneData*)gpuSceneDataBuffer.allocation->GetMappedData();
    GPUSceneData* sceneUniformData = (GPUSceneData*)gpuSceneDataBuffer.info.pMappedData;
    *sceneUniformData = sceneData; // Copy current scene data

    currentFrame._deletionQueue.push_function([=, this]() {
        engine->destroy_buffer(gpuSceneDataBuffer);
    });

    writer.write_buffer(0, gpuSceneDataBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    //skybox cube map texture that gets sent to the skybox shader to render the skybox
    writer.write_image(3, _cubeMapHDRTexture.imageView, _defaultCubeSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.write_image(4, _irradianceMapTexture.imageView, _defaultCubeSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.update_set(_device, globalDescriptor);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _skyboxPipelineLayout, 0, 1, &globalDescriptor, 0, nullptr);

    vkCmdBindIndexBuffer(cmd, engine->cube.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

    GPUDrawPushConstants pushConstants;
    pushConstants.vertexBuffer = engine->cube.vertexBufferAddress;
    
    // This effectively removes the translation from the view matrix
    pushConstants.worldMatrix = glm::translate(glm::mat4{ 1.f }, engine->mainCamera.position);

    vkCmdPushConstants(cmd, _skyboxPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GPUDrawPushConstants), &pushConstants);

    vkCmdDrawIndexed(cmd, 36, 1, 0, 0, 0);
    
 
}
