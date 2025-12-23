#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <cstdint>
#include <vk_initializers.h>
#include <vk_types.h>
#include "vk_images.h"

#include "VkBootstrap.h"
#include "vulkan/vulkan_core.h"

#include "vk_pipelines.h"

#include <chrono>
#include <thread>
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

VulkanEngine* loadedEngine = nullptr;


VulkanEngine& VulkanEngine::Get() { return *loadedEngine; }


constexpr bool bUseValidationLayers = true; //set this to true if debugging


void VulkanEngine::init()
{
    // only one engine initialization is allowed with the application.
    assert(loadedEngine == nullptr);
    loadedEngine = this;

    // We initialize SDL and create a window with it.
    SDL_Init(SDL_INIT_VIDEO);

    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);

    _window = SDL_CreateWindow(
        "Vulkan Engine",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        _windowExtent.width,
        _windowExtent.height,
        window_flags);

        init_vulkan();
        init_swapchain();
        init_commands();
        init_sync_structures();
        init_descriptors();
        init_pipelines();

    // everything went fine
    _isInitialized = true;
}

//the dependencies must be destroyed in the following order
// CommandPool -> Swapchain -> Surface -> Instance -> SDL_window
// Device doesn't get destroyed because it's just a handle to the GPU.
void VulkanEngine::cleanup()
{
    if (_isInitialized) {
        //make sure the gpu has stopped doing its things
    		vkDeviceWaitIdle(_device);

    		for (int i = 0; i < FRAME_OVERLAP; i++) {
            vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);
            //destroy sync objects
            vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
            vkDestroySemaphore(_device, _frames[i]._renderSemaphore, nullptr);
            vkDestroySemaphore(_device ,_frames[i]._swapchainSemaphore, nullptr);
            _frames[i]._deletionQueue.flush();
    		}
        _mainDeletionQueue.flush();


    		destroy_swapchain();

    		vkDestroySurfaceKHR(_instance, _surface, nullptr);
    		vkDestroyDevice(_device, nullptr);
		
    		vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
    		vkDestroyInstance(_instance, nullptr);
    		SDL_DestroyWindow(_window);
    	}

    // clear engine pointer
    loadedEngine = nullptr;
}

void VulkanEngine::draw()
{
    // wait until the gpu has finished rendering the last frame. Timeout of 1
    // second
    // It’s using nanoseconds for the wait time. If you call the function with 0
    // as the timeout, you can use it to know if the GPU is still executing the command or not.
    VK_CHECK(vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, 1000000000));

    get_current_frame()._deletionQueue.flush();

    //Fences have to be reset between uses, you can’t use the same fence on multiple
    // GPU commands without resetting it in the middle.
    VK_CHECK(vkResetFences(_device, 1, &get_current_frame()._renderFence));
    uint32_t swapchainImageIndex;
    VK_CHECK(vkAcquireNextImageKHR(_device, _swapchain, 1000000000, get_current_frame()._swapchainSemaphore, nullptr, &swapchainImageIndex));
    //naming it cmd for shorter writing
    VkCommandBuffer cmd = get_current_frame()._mainCommandBuffer;

    //at this point the GPU should have unlocked everything so the
    // command buffer is free to use for the cpu
    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    VkCommandBufferBeginInfo cmdInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    _drawExtent.width = _drawImage.imageExtent.width;
    _drawExtent.height = _drawImage.imageExtent.height;

    //start the command buffer recording
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdInfo));

    //make the swapchain image into writeable mode before rendering
    vkutil::transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);


    draw_background(cmd);

    //make the swapchain image into presentable mode
    
    vkutil::transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    
    vkutil::transition_image(cmd , _swapchainImages[swapchainImageIndex],VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // execute a copy from the draw image into the swapchain
    vkutil::copy_image_to_image(cmd, _drawImage.image, _swapchainImages[swapchainImageIndex], _drawExtent, _swapchainExtent);

    // set swapchain image layout to Present so we can show it on the screen
    vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    //finalize the command buffer (we can no longer add commands, but it can now be executed)
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmdSubmitInfo = vkinit::command_buffer_submit_info(cmd);
    VkSemaphoreSubmitInfo waitSemaphore = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, get_current_frame()._swapchainSemaphore);
    VkSemaphoreSubmitInfo signalSemaphore = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, get_current_frame()._renderSemaphore);

    VkSubmitInfo2 submit = vkinit::submit_info(&cmdSubmitInfo, &signalSemaphore, &waitSemaphore);

    //submit command buffer to the queue and execute it.
    // _renderFence will now block until the graphic commands finish execution
    VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, get_current_frame()._renderFence));


    //prepare present
    // this will put the image we just rendered to into the visible window.
    // we want to wait on the _renderSemaphore for that, 
    // as its necessary that drawing commands have finished before the image is displayed to the user
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = nullptr;
    presentInfo.pSwapchains = &_swapchain;
    presentInfo.swapchainCount = 1;
    
    presentInfo.pWaitSemaphores = &get_current_frame()._renderSemaphore;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pImageIndices = &swapchainImageIndex;
    VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));
    _frameNumber++;

}

void VulkanEngine::draw_background(VkCommandBuffer cmd){
    

    //make a clear-color from frame number. This will flash with a 120 frame period.
    // clear color is the color to set to when the frame is cleared
    VkClearColorValue clearValue;
    float flash = std::abs(std::sin(_frameNumber / 120.f));
    float flashr = std::abs(std::cos(_frameNumber / 120.f));
    clearValue = { { flashr, 0.0f, flash, 1.0f } };

    VkImageSubresourceRange clearRange = vkinit::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);

    //clear image
    // vkCmdClearColorImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);
    // bind the gradient drawing compute pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _gradientPipeline);

    // bind the descriptor set containing the draw image for the compute pipeline
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _gradientPipelineLayout, 0, 1, &_drawImageDescriptors, 0, nullptr);

    _breathColorPushConst.inColorX = std::abs(std::sin(_frameNumber / 120.f));
    _breathColorPushConst.inColorY = std::abs(std::cos(_frameNumber / 120.f));

   //push constant pushing    
    vkCmdPushConstants(cmd, _gradientPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstant), &_breathColorPushConst);
    // execute the compute pipeline dispatch. We are using 16x16 workgroup size so we need to divide by it
    vkCmdDispatch(cmd, std::ceil(_drawExtent.width / 16.0), std::ceil(_drawExtent.height / 16.0), 1);
    // fmt::print("{} {}", _drawExtent.width, _drawExtent.height);

    
}

void VulkanEngine::run()
{
// 	typedef struct SDL_KeyboardEvent
// {
//     Uint32 type;        /**< SDL_KEYDOWN or SDL_KEYUP */
//     Uint32 timestamp;   /**< In milliseconds, populated using SDL_GetTicks() */
//     Uint32 windowID;    /**< The window with keyboard focus, if any */
//     Uint8 state;        /**< SDL_PRESSED or SDL_RELEASED */
//     Uint8 repeat;       /**< Non-zero if this is a key repeat */
//     Uint8 padding2;
//     Uint8 padding3;
//     SDL_Keysym keysym;  /**< The key that was pressed or released */
// } SDL_KeyboardEvent;
    SDL_Event e;
    bool bQuit = false;

    // main loop
    while (!bQuit) {
        // Handle events on queue
        while (SDL_PollEvent(&e) != 0) {
            // close the window when user alt-f4s or clicks the X button
            if (e.type == SDL_QUIT)
                bQuit = true;

            if (e.type == SDL_WINDOWEVENT) {
                if (e.window.event == SDL_WINDOWEVENT_MINIMIZED) {
                    stop_rendering = true;
                }
                if (e.window.event == SDL_WINDOWEVENT_RESTORED) {
                    stop_rendering = false;
                }
            }
            if(e.type == SDL_KEYDOWN){
            	if(e.key.state == SDL_PRESSED){
            		fmt::print("key pressed: {}\n", static_cast<char>(e.key.keysym.sym));
            	}
            	
            }
        }

        // do not draw if we are minimized
        if (stop_rendering) {
            // throttle the speed to avoid the endless spinning
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        draw();
    }
}


//initializes the vulkan instance so we can call vulkan functions.
void VulkanEngine::init_vulkan(){
    vkb::InstanceBuilder builder; //class made in bootstrap module to handle creation of vkb

    //handles all of the boiler plate code for creating a vulkan instance
    auto inst_ret = builder.set_app_name("Z Engine R")
        .request_validation_layers(bUseValidationLayers) //use the specified validation layers
        .use_default_debug_messenger()
        .require_api_version(1, 3, 0) //api version 1.3.0
        .build();

    vkb::Instance vkb_inst = inst_ret.value();
    _instance = vkb_inst.instance;
    _debug_messenger = vkb_inst.debug_messenger;

    SDL_Vulkan_CreateSurface(_window, _instance, &_surface);

    //vulkan 1.3 features
    VkPhysicalDeviceVulkan13Features features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    features.dynamicRendering = true;
    features.synchronization2 = true;

    //vulkan 1.2 features
    VkPhysicalDeviceVulkan12Features features12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    features12.bufferDeviceAddress = true;
    features12.descriptorIndexing = true;

    //obtains the devices gpu using vkbootsrap
    vkb::PhysicalDeviceSelector selector{ vkb_inst };
    vkb::PhysicalDevice physicalDevice = selector
		.set_minimum_version(1, 3)
		.set_required_features_13(features)
		.set_required_features_12(features12)
		.set_surface(_surface)
		.select()
		.value();

    //create the final vulkan device
    vkb::DeviceBuilder deviceBuilder{ physicalDevice };

    vkb::Device vkbDevice = deviceBuilder.build().value();

    // Get the VkDevice handle used in the rest of a vulkan application
    _device = vkbDevice.device;
    _chosenGPU = physicalDevice.physical_device;

    // use vkbootstrap to get a Graphics queue
    _graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    _graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = _chosenGPU;
    allocatorInfo.device = _device;
    allocatorInfo.instance = _instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaCreateAllocator(&allocatorInfo, &_allocator);

    _mainDeletionQueue.push_function([&]() {
        vmaDestroyAllocator(_allocator);
    });
}



//creates the swapchain and sets the _swapchain, _swapchainImages, and _swapchainImageViews.
void VulkanEngine::create_swapchain(uint32_t width, uint32_t height){

	vkb::SwapchainBuilder swapchainBuilder{ _chosenGPU,_device,_surface };

	_swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

	vkb::Swapchain vkbSwapchain = swapchainBuilder
		//.use_default_format_selection()
		.set_desired_format(VkSurfaceFormatKHR{ .format = _swapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
		//use vsync present mode
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		.set_desired_extent(width, height)
		.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		.build()
		.value();

	_swapchainExtent = vkbSwapchain.extent;
	//store swapchain and its related images
	_swapchain = vkbSwapchain.swapchain;
	_swapchainImages = vkbSwapchain.get_images().value();
	_swapchainImageViews = vkbSwapchain.get_image_views().value();
}



void VulkanEngine::init_swapchain(){
    create_swapchain(_windowExtent.width, _windowExtent.height);
    VkExtent3D drawImageExtent = {
        _windowExtent.width,
        _windowExtent.height,
        1
    };
    //hardcoding the draw format to 32 bit float
    _drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    _drawImage.imageExtent = drawImageExtent;

    VkImageUsageFlags drawImageUsages{};
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkImageCreateInfo rimg_info = vkinit::image_create_info(_drawImage.imageFormat, drawImageUsages, drawImageExtent);
    //for the draw image, we want to allocate it from gpu local memory
    VmaAllocationCreateInfo rimg_allocinfo = {};
    rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    //allocate and create the image
    vmaCreateImage(_allocator, &rimg_info, &rimg_allocinfo, &_drawImage.image, &_drawImage.allocation, nullptr);

    //build a image-view for the draw image to use for rendering
    VkImageViewCreateInfo rview_info = vkinit::imageview_create_info(_drawImage.imageFormat, _drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
    VK_CHECK(vkCreateImageView(_device, &rview_info, nullptr, &_drawImage.imageView));

    	//add to deletion queues
    	_mainDeletionQueue.push_function([=]() {
    		vkDestroyImageView(_device, _drawImage.imageView, nullptr);
    		vmaDestroyImage(_allocator, _drawImage.image, _drawImage.allocation);
    	});
}



void VulkanEngine::destroy_swapchain()
{
	vkDestroySwapchainKHR(_device, _swapchain, nullptr);

	// destroy swapchain resources
	for (int i = 0; i < _swapchainImageViews.size(); i++) {

		vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
	}
}



void VulkanEngine::init_commands(){
    //create a command pool for commands submitted to the graphics queue.
    //we also want the pool to allow for resetting of individual command buffers
    // VkCommandPoolCreateInfo commandPoolInfo =  {};
    // commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    // commandPoolInfo.pNext = nullptr;
    // commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    // commandPoolInfo.queueFamilyIndex = _graphicsQueueFamily;

    // for (int i = 0; i < FRAME_OVERLAP; i++) {

    //     VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i]._commandPool));

    //     // allocate the default command buffer that we will use for rendering
    //     // By doing the ` = {}` thing, we are letting the compiler initialize
    //     // the entire struct to zero. This is critical, as in general Vulkan
    //     // structs will have their defaults set in a way that 0 is relatively safe.
    //     // By doing that, we make sure we don’t leave uninitialized data in the struct.
    //     VkCommandBufferAllocateInfo cmdAllocInfo = {};
    //     cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    //     cmdAllocInfo.pNext = nullptr;
    //     cmdAllocInfo.commandPool = _frames[i]._commandPool;
    //     cmdAllocInfo.commandBufferCount = 1;
    //     cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    //     VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i]._mainCommandBuffer));
    // }



    //there is abstraction code already in vk_initializers
    // so I'm going to use that to rewrite the above code

    VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily, 
                                                VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

    for (int i = 0; i < FRAME_OVERLAP; i++){
        VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i]._commandPool));

        VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_frames[i]._commandPool, 1);     
        VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i]._mainCommandBuffer));
    }
    
    
}

void VulkanEngine::init_sync_structures(){
    VkFenceCreateInfo fenceInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);

    VkSemaphoreCreateInfo semaphorInfo = vkinit::semaphore_create_info();

    for(int i = 0; i < FRAME_OVERLAP; i++){
        VK_CHECK(vkCreateSemaphore(_device, &semaphorInfo, nullptr, &_frames[i]._swapchainSemaphore));
        VK_CHECK(vkCreateSemaphore(_device, &semaphorInfo, nullptr, &_frames[i]._renderSemaphore));
        VK_CHECK(vkCreateFence(_device, &fenceInfo, nullptr, &_frames[i]._renderFence));
    }
    
}

void VulkanEngine::init_descriptors(){
    //create a descriptor pool that will hold 10 sets with 1 image each
    std::vector<DescriptorAllocator::PoolSizeRatio> sizes =
    {
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }
    };

    globalDescriptorAllocator.init_pool(_device, 10, sizes);

    //make the descriptor set layout for our compute draw
    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        _drawImageDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);
    }

    //allocate a descriptor set for our draw image
    _drawImageDescriptors = globalDescriptorAllocator.allocate(_device,_drawImageDescriptorLayout);	

    VkDescriptorImageInfo imgInfo{};
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imgInfo.imageView = _drawImage.imageView;

    VkWriteDescriptorSet drawImageWrite = {};
    drawImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    drawImageWrite.pNext = nullptr;

    drawImageWrite.dstBinding = 0;
    drawImageWrite.dstSet = _drawImageDescriptors;
    drawImageWrite.descriptorCount = 1;
    drawImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    drawImageWrite.pImageInfo = &imgInfo;

    vkUpdateDescriptorSets(_device, 1, &drawImageWrite, 0, nullptr);

    //make sure both the descriptor allocator and the new layout get cleaned up properly
    _mainDeletionQueue.push_function([&]() {
        globalDescriptorAllocator.destroy_pool(_device);

        vkDestroyDescriptorSetLayout(_device, _drawImageDescriptorLayout, nullptr);
    });
}

void VulkanEngine::init_pipelines(){
    init_background_pipelines();
}

void VulkanEngine::init_background_pipelines(){
    //setting up push constants to inject into the layout info
    VkPushConstantRange pushConstant;
    pushConstant.offset = 0; //the push constant range starts at 0
    pushConstant.size =sizeof(ComputePushConstant); //setting the size of the pushConstant
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        
    
    //creating the layout info for the pipeline
    VkPipelineLayoutCreateInfo computeLayout{};
    computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    computeLayout.pNext = nullptr;
    computeLayout.pSetLayouts = &_drawImageDescriptorLayout;
    computeLayout.setLayoutCount = 1;
    computeLayout.pPushConstantRanges = &pushConstant;
    computeLayout.pushConstantRangeCount = 1;

    VK_CHECK(vkCreatePipelineLayout(_device, &computeLayout, nullptr, &_gradientPipelineLayout));


    //layout code
    VkShaderModule computeDrawShader;
    if (!vkutil::load_shader_module("shaders/gradient.comp.spv", _device, &computeDrawShader))
    {
        fmt::print("Error when building the compute shader \n");
    }

    VkPipelineShaderStageCreateInfo stageinfo{};
    stageinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageinfo.pNext = nullptr;
    stageinfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageinfo.module = computeDrawShader;
    //note here is that we are giving
    // it the name of the function we want
    // the shader to use, which is going to be main().
    stageinfo.pName = "main";

    VkComputePipelineCreateInfo computePipelineCreateInfo{};
    computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computePipelineCreateInfo.pNext = nullptr;
    computePipelineCreateInfo.layout = _gradientPipelineLayout;
    computePipelineCreateInfo.stage = stageinfo;

    VK_CHECK(vkCreateComputePipelines(_device,VK_NULL_HANDLE,1,&computePipelineCreateInfo, nullptr, &_gradientPipeline));
    vkDestroyShaderModule(_device, computeDrawShader, nullptr);

    _mainDeletionQueue.push_function([&]() {
    		vkDestroyPipelineLayout(_device, _gradientPipelineLayout, nullptr);
    		vkDestroyPipeline(_device, _gradientPipeline, nullptr);
		});
}
