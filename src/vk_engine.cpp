#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
//#include <sys/_types/_u_int32_t.h>
#include <cstdint>
#include <vk_initializers.h>
#include <vk_types.h>
#include "vk_descriptors.h"
#include "vk_images.h"

#include "VkBootstrap.h"
#include "vk_loader.h"
#include "vulkan/vulkan_core.h"

#include "vk_pipelines.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"

#include <chrono>
#include <thread>
#include <glm/gtx/transform.hpp>
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include <stb_image.h>

VulkanEngine* loadedEngine = nullptr;


VulkanEngine& VulkanEngine::Get() { return *loadedEngine; }


constexpr bool bUseValidationLayers = true; //set this to true if debugging

//checks if the mesh is within the viewing frustum. It's frustum culling.
bool is_visible(const RenderObject& obj, const glm::mat4& viewproj) {
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

void VulkanEngine::init(){
    // only one engine initialization is allowed with the application.
    assert(loadedEngine == nullptr);
    loadedEngine = this;

    // We initialize SDL and create a window with it.
    SDL_Init(SDL_INIT_VIDEO);

    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

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
        init_imgui();

    // everything went fine
    _isInitialized = true;

    /////////Initialize main camera////////////
    mainCamera.velocity = glm::vec3(0.f);
    mainCamera.position = glm::vec3(0, 0, 10);
    // mainCamera.position = glm::vec3(30.f, 1.f, 5.f);
    mainCamera.pitch = 0;
    mainCamera.yaw = 0;
    ///////////////////////

    /////////////////Load the scene data////////////////
    std::string structurePath = { "assets/structure.glb" };
    std::string cityPath = {"assets/VirtualCity.glb"};
    std::string donutPath = {"assets/donut.glb"};
    std::string balloonPath = {"assets/balloon.glb"};
    std::string blowDartPath = {"assets/aztec_blowgun_and_darts.glb"};
    std::string helmetPath = {"assets/DamagedHelmet.glb"};
    
    auto structureFile = loadGltf(this,structurePath);
    assert(structureFile.has_value());
    
    auto cityFile = loadGltf(this, cityPath);
    assert(cityFile.has_value());

    auto donutFile = loadGltf(this, donutPath);
    assert(donutFile.has_value());

    auto balloonFile = loadGltf(this, balloonPath);
    assert(balloonFile.has_value());

    auto blowDartFile = loadGltf(this, blowDartPath);
    assert(blowDartFile.has_value());

    auto helmetFile = loadGltf(this, helmetPath);
    assert(helmetFile.has_value());

    loadedScenes["structure"] = *structureFile;
    loadedScenes["virtual city"] = *cityFile;
    loadedScenes["donut"] = *donutFile;
    loadedScenes["balloon"] = *balloonFile;
    loadedScenes["blowDart"] = *blowDartFile;
    loadedScenes["helmet"] = *helmetFile;
    /////////////////////////////////
    
    ////////////////// initialize lighting parameters for blinn-phong illumination models
    sceneData.ambientColor = glm::vec4(0.1f, 0.1f, 0.2f, 1.0f);
    sceneData.specularColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    sceneData.sunlightColor = glm::vec4(1.0f, 0.95f, 0.8f, 1.0f);
    sceneData.sunlightDirection = glm::vec4(0,1,0.5,1.f);
    sceneData.shininess = 100;
    ////////////////////////////////////////////////////

    /////////Initialize light camera////////////
    lightCamera.velocity = glm::vec3(0.f);
    lightCamera.position = 100.f * sceneData.sunlightDirection;
    lightCamera.pitch = 0;
    lightCamera.yaw = 0;
    ///////////////////////////////////////
}

//the dependencies must be destroyed in the following order
// loadedScenes -> CommandPool -> Swapchain -> Surface -> Instance -> SDL_window
// Device doesn't get destroyed because it's just a handle to the GPU.
void VulkanEngine::cleanup(){
    if (_isInitialized) {
        //make sure the gpu has stopped doing its things
    		vkDeviceWaitIdle(_device);
        loadedScenes.clear();

        // metalRoughMaterial.clear_resources(_device);                                 
    		for (int i = 0; i < FRAME_OVERLAP; i++) {
            vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);
            //destroy sync objects
            vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
            vkDestroySemaphore(_device, _frames[i]._renderSemaphore, nullptr);
            vkDestroySemaphore(_device ,_frames[i]._swapchainSemaphore, nullptr);
            _frames[i]._deletionQueue.flush();
    		}
    		for(auto& mesh : testMeshes){
    		    destroy_buffer(mesh->meshBuffers.indexBuffer);
            destroy_buffer(mesh->meshBuffers.vertexBuffer);
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

//main rendering function to display an image on to the screen
void VulkanEngine::draw(){

    auto start = std::chrono::system_clock::now();
    //sets the parameters for updating the scenes.
    update_scene();
    auto end = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    stats.scene_update_time = elapsed.count() / 1000.f;
    
    // wait until the gpu has finished rendering the last frame. Timeout of 1
    // second
    // It’s using nanoseconds for the wait time. If you call the function with 0
    // as the timeout, you can use it to know if the GPU is still executing the command or not.
    VK_CHECK(vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, 1000000000));
  
    get_current_frame()._deletionQueue.flush();
    get_current_frame()._frameDescriptors.clear_pools(_device);
 
    //Fences have to be reset between uses, you can’t use the same fence on multiple
    // GPU commands without resetting it in the middle.
    VK_CHECK(vkResetFences(_device, 1, &get_current_frame()._renderFence));
    uint32_t swapchainImageIndex;
    VkResult e = vkAcquireNextImageKHR(_device, _swapchain, 1000000000, get_current_frame()._swapchainSemaphore, nullptr, &swapchainImageIndex);
    if (e == VK_ERROR_OUT_OF_DATE_KHR) {
        _resize_requested = true;       
    		return ;
    }
    //naming it cmd for shorter writing
    VkCommandBuffer cmd = get_current_frame()._mainCommandBuffer;

    //at this point the GPU should have unlocked everything so the
    // command buffer is free to use for the cpu
    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    VkCommandBufferBeginInfo cmdInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    // _drawExtent.width = _drawImage.imageExtent.width;
    // _drawExtent.height = _drawImage.imageExtent.height;
    _drawExtent.height = std::min(_swapchainExtent.height, _drawImage.imageExtent.height) * renderScale;
    _drawExtent.width= std::min(_swapchainExtent.width, _drawImage.imageExtent.width) * renderScale;

    //start the command buffer recording
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdInfo));

    //make the swapchain image into writeable mode before rendering
    vkutil::transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);


    draw_background(cmd);

    vkutil::transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    vkutil::transition_image(cmd, _depthImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    //render the shadow map by making it writable as a depth buffer then
    //changing it into a readable texture for draw_geometry
    vkutil::transition_image(cmd, _lightDepthImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    render_shadow_map(cmd);
    // vkutil::transition_image(cmd, _lightDepthImage.image, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    /// manual transition because the transition image function will convert to VK_IMAGE_ASPECT_COLOR_BIT
    // if newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    // TODO: MODIFY transition image.
    VkImageMemoryBarrier2 imageBarrier {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    imageBarrier.pNext = nullptr;

    imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;

    imageBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    imageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkImageAspectFlags aspectMask =  VK_IMAGE_ASPECT_DEPTH_BIT;
    imageBarrier.subresourceRange = vkinit::image_subresource_range(aspectMask);
    imageBarrier.image = _lightDepthImage.image;

    VkDependencyInfo depInfo {};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.pNext = nullptr;

    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &imageBarrier;

    vkCmdPipelineBarrier2(cmd, &depInfo);
    ///////////////////////////////


    draw_geometry(cmd);

    //make the swapchain image into presentable mode
    
    vkutil::transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    
    vkutil::transition_image(cmd , _swapchainImages[swapchainImageIndex],VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // execute a copy from the draw image into the swapchain
    vkutil::copy_image_to_image(cmd, _drawImage.image, _swapchainImages[swapchainImageIndex], _drawExtent, _swapchainExtent);

    // set swapchain image layout to Attachment Optimal so we can draw it
    vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    //draw imgui into the swapchain image
    draw_imgui(cmd,  _swapchainImageViews[swapchainImageIndex]);

    // set swapchain image layout to Present so we can show it on the screen
    vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

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
    VkResult presentResult = vkQueuePresentKHR(_graphicsQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR) {
        _resize_requested = true;
    }
    _frameNumber++;

}

// main compute shader drawing function for the background
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
    ComputeEffect& effect = backgroundEffects[currentBackgroundEffect];
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);

    // bind the descriptor set containing the draw image for the compute pipeline
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _gradientPipelineLayout, 0, 1, &_drawImageDescriptors, 0, nullptr);

    // _breathColorPushConst.inColorX = std::abs(std::sin(_frameNumber / 120.f));
    // _breathColorPushConst.inColorY = std::abs(std::cos(_frameNumber / 120.f));

    // ComputePushConstant pc;
    // pc.data1 = glm::vec4(1, 0, 0, 1);
    // pc.data2 = glm::vec4(0, 0, 1, 1);

   //push constant pushing    
    vkCmdPushConstants(cmd, _gradientPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstant), &effect.data);
    // execute the compute pipeline dispatch. We are using 16x16 workgroup size so we need to divide by it
    vkCmdDispatch(cmd, std::ceil(_drawExtent.width / 16.0), std::ceil(_drawExtent.height / 16.0), 1);
    
}

// the while 0 loop of the engine. Everything that needs to run in a loop goes here
void VulkanEngine::run(){
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
        auto start = std::chrono::system_clock::now();
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
            // if(e.type == SDL_KEYDOWN){
            // 	if(e.key.state == SDL_PRESSED){
            // 		fmt::print("key pressed: {}\n", static_cast<char>(e.key.keysym.sym));
            // 	}
            	
            // }
            mainCamera.processSDLEvent(e);
            ImGui_ImplSDL2_ProcessEvent(&e);
        }

        // do not draw if we are minimized
        if (stop_rendering) {
            // throttle the speed to avoid the endless spinning
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if(_resize_requested){
            resize_swapchain();
        }

        // imgui new frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        //some imgui UI to test
        // ImGui::ShowDemoWindow();
        if (ImGui::Begin("background")) {
    			ImGui::SliderFloat("Render Scale",&renderScale, 0.3f, 1.f);

    			ComputeEffect& selected = backgroundEffects[currentBackgroundEffect];
		
    			ImGui::Text("Selected effect: ", selected.name);
		
    			ImGui::SliderInt("Effect Index", &currentBackgroundEffect,0, backgroundEffects.size() - 1);
		
    			ImGui::InputFloat4("data1",(float*)& selected.data.data1);
    			ImGui::InputFloat4("data2",(float*)& selected.data.data2);
    			ImGui::InputFloat4("data3",(float*)& selected.data.data3);
    			ImGui::InputFloat4("data4",(float*)& selected.data.data4);
    		}

	  		ImGui::End();

	  		ImGui::Begin("Stats");

        ImGui::Text("frametime %f ms", stats.frametime);
        ImGui::Text("fps %i", stats.fps);
        ImGui::Text("draw time %f ms", stats.mesh_draw_time);
        ImGui::Text("update time %f ms", stats.scene_update_time);
        ImGui::Text("triangles %i", stats.triangle_count);
        ImGui::Text("draws %i", stats.drawcall_count);
        ImGui::End();

        if (ImGui::Begin("sceneData")) {
    			ImGui::InputFloat4("Ambient Color",(float*)& sceneData.ambientColor);
    			ImGui::InputFloat4("Diffuse Color",(float*)& sceneData.sunlightColor);
    			ImGui::InputFloat4("Specular Color", (float*)& sceneData.specularColor);
    			ImGui::InputFloat4("sun light direction",(float*)& sceneData.sunlightDirection);
    			ImGui::InputInt("shininess", &sceneData.shininess);
    			// ImGui::InputFloat4("data4",(float*)& selected.d ata.data4);
    		}

    		ImGui::End();

    		if (ImGui::Begin("PBR rendering components")){
    	
    			ImGui::InputFloat4("Color Factors",(float*)& colorFactors);
    			ImGui::InputFloat4("metal rough factors",(float*)& metal_rough_factors);
    		}
  
    		ImGui::End();

        /////////debugging use only
    		GLTFMetallic_Roughness::MaterialConstants* materialData = (GLTFMetallic_Roughness::MaterialConstants*)materialConstants.allocation->GetMappedData();
        materialData->colorFactors = colorFactors;
        materialData->metal_rough_factors = metal_rough_factors;
        /////////////////
    
        //make imgui calculate internal draw structures
        ImGui::Render();

        draw();

        auto end = std::chrono::system_clock::now();

        //convert to microseconds (integer), and then come back to miliseconds
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        stats.frametime = elapsed.count() / 1000.f;
        stats.fps = (int)(1/(stats.frametime/1000.f));
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

    VkPhysicalDeviceFeatures wireFeatures{};
    wireFeatures.fillModeNonSolid = VK_TRUE;
    
    //obtains the devices gpu using vkbootsrap
    vkb::PhysicalDeviceSelector selector{ vkb_inst };
    vkb::PhysicalDevice physicalDevice = selector
		.set_minimum_version(1, 3)
		.set_required_features_13(features)
		.set_required_features_12(features12)
		.set_required_features(wireFeatures)
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
		// .set_desired_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
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

    VkExtent3D lightImageExtent = {
        1024,
        1024,
        1
    };
    //hardcoding the draw format to 64 bit float
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
    VkImageViewCreateInfo rview_info = vkinit::imageview_create_info(_drawImage.imageFormat, _drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D);
    VK_CHECK(vkCreateImageView(_device, &rview_info, nullptr, &_drawImage.imageView));

    _depthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
    _depthImage.imageExtent = drawImageExtent;
    VkImageUsageFlags depthImageUsages{};
    depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthImageUsages |= VK_IMAGE_USAGE_SAMPLED_BIT;

    VkImageCreateInfo dimg_info = vkinit::image_create_info(_depthImage.imageFormat, depthImageUsages, drawImageExtent);

    //allocate and create the image
    vmaCreateImage(_allocator, &dimg_info, &rimg_allocinfo, &_depthImage.image, &_depthImage.allocation, nullptr);

    //build a image-view for the draw image to use for rendering
    VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(_depthImage.imageFormat, _depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_VIEW_TYPE_2D);

    VK_CHECK(vkCreateImageView(_device, &dview_info, nullptr, &_depthImage.imageView));


    /////////////////////Depth map for shadow mapping //////////////////

    _lightDepthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
    _lightDepthImage.imageExtent = lightImageExtent; 

    VkImageUsageFlags lightDepthImageUsages{};
    lightDepthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    lightDepthImageUsages |= VK_IMAGE_USAGE_SAMPLED_BIT;

    VkImageCreateInfo ldimg_info = vkinit::image_create_info(_lightDepthImage.imageFormat, lightDepthImageUsages, lightImageExtent);
    vmaCreateImage(_allocator, &ldimg_info, &rimg_allocinfo, &_lightDepthImage.image, &_lightDepthImage.allocation, nullptr);

    VkImageViewCreateInfo ldview_info = vkinit::imageview_create_info(_lightDepthImage.imageFormat, _lightDepthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_VIEW_TYPE_2D);
    VK_CHECK(vkCreateImageView(_device, &ldview_info, nullptr, &_lightDepthImage.imageView));

    /////////////////////////////////////////
    
    	//add to deletion queues
    	_mainDeletionQueue.push_function([=]() {
    		vkDestroyImageView(_device, _drawImage.imageView, nullptr);
    		vmaDestroyImage(_allocator, _drawImage.image, _drawImage.allocation);
    		vkDestroyImageView(_device, _depthImage.imageView, nullptr);
    		vmaDestroyImage(_allocator, _depthImage.image, _depthImage.allocation);
    		vkDestroyImageView(_device, _lightDepthImage.imageView, nullptr);
    		vmaDestroyImage(_allocator, _lightDepthImage.image, _lightDepthImage.allocation);
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


void VulkanEngine::resize_swapchain(){
    vkDeviceWaitIdle(_device);

    destroy_swapchain();

    int w, h;
    SDL_GetWindowSize(_window, &w, &h);
    _windowExtent.width = w;
    _windowExtent.height = h;

    create_swapchain(_windowExtent.width, _windowExtent.height);

    _resize_requested = false;
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
    
    VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_imCommandPool));
    VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_imCommandPool, 1);
    VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_imCommandBuffer));

    _mainDeletionQueue.push_function([=](){
            vkDestroyCommandPool(_device, _imCommandPool, nullptr);
         });
}


void VulkanEngine::init_sync_structures(){
    VkFenceCreateInfo fenceInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);

    VkSemaphoreCreateInfo semaphorInfo = vkinit::semaphore_create_info();

    for(int i = 0; i < FRAME_OVERLAP; i++){
        VK_CHECK(vkCreateSemaphore(_device, &semaphorInfo, nullptr, &_frames[i]._swapchainSemaphore));
        VK_CHECK(vkCreateSemaphore(_device, &semaphorInfo, nullptr, &_frames[i]._renderSemaphore));
        VK_CHECK(vkCreateFence(_device, &fenceInfo, nullptr, &_frames[i]._renderFence));
    }

    VK_CHECK(vkCreateFence(_device, &fenceInfo, nullptr, &_imFence));
    _mainDeletionQueue.push_function([=](){
        vkDestroyFence(_device, _imFence, nullptr);
     });
    
}


void VulkanEngine::init_descriptors(){
    //create a descriptor pool that will hold 10 sets with 1 image each
    std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes =
    {
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 }, 
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 }, 
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
    };

    globalDescriptorAllocator.init(_device, 10, sizes);

    //make the descriptor set layout for our compute draw
    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        _drawImageDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);
    }

    //main image Descriptor set layout
    {
    	DescriptorLayoutBuilder builder;
    	builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    	builder.add_binding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    	builder.add_binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER); // ShadowMap Texture
    	builder.add_binding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER); //skybox
    	builder.add_binding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER); //irradiance map
    	builder.add_binding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER); //brdfLUT;
    	_gpuSceneDataDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    }

    //shadow map Descriptor set layuout
    {
    	DescriptorLayoutBuilder builder;
    	builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    	_shadowSceneDataDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_VERTEX_BIT );
    }

    {
    	DescriptorLayoutBuilder builder;
    	builder.add_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    	_singleImageDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_FRAGMENT_BIT);
    }
    
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
    
    // {
    //     DescriptorLayoutBuilder builder;
    //     builder.add_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    //     builder.add_binding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    //     _preFilterDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);
        
    // }

    
    //allocate a descriptor set for our draw image
    _drawImageDescriptors = globalDescriptorAllocator.allocate(_device, _drawImageDescriptorLayout);	
    _rectToCubeDescriptor = globalDescriptorAllocator.allocate(_device, _rectToCubeDescriptorLayout);
    _irradianceDescriptor = globalDescriptorAllocator.allocate(_device, _rectToCubeDescriptorLayout);
    // _preFilterDescriptor = globalDescriptorAllocator.allocate(_device, _rectToCubeDescriptorLayout);
    _brdfLUTDescriptor = globalDescriptorAllocator.allocate(_device, _brdfLUTDescriptorLayout);
    

    // VkDescriptorImageInfo imgInfo{};
    // imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    // imgInfo.imageView = _drawImage.imageView;

    // VkWriteDescriptorSet drawImageWrite = {};
    // drawImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    // drawImageWrite.pNext = nullptr;

    // drawImageWrite.dstBinding = 0;
    // drawImageWrite.dstSet = _drawImageDescriptors;
    // drawImageWrite.descriptorCount = 1;
    // drawImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    // drawImageWrite.pImageInfo = &imgInfo;

    // vkUpdateDescriptorSets(_device, 1, &drawImageWrite, 0, nullptr);

    DescriptorWriter writer;
    writer.write_image(0, _drawImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    writer.update_set(_device, _drawImageDescriptors);

    //make sure both the descriptor allocator and the new layout get cleaned up properly
    _mainDeletionQueue.push_function([&]() {
        globalDescriptorAllocator.destroy_pools(_device);

        vkDestroyDescriptorSetLayout(_device, _drawImageDescriptorLayout, nullptr);
        vkDestroyDescriptorSetLayout(_device, _gpuSceneDataDescriptorLayout, nullptr);
        vkDestroyDescriptorSetLayout(_device, _singleImageDescriptorLayout, nullptr);
        vkDestroyDescriptorSetLayout(_device, _shadowSceneDataDescriptorLayout, nullptr);
        vkDestroyDescriptorSetLayout(_device, _rectToCubeDescriptorLayout, nullptr);
        vkDestroyDescriptorSetLayout(_device, _brdfLUTDescriptorLayout, nullptr);
    });

    for (int i = 0; i < FRAME_OVERLAP; i++) {
    		// create a descriptor pool
    		std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frame_sizes = { 
    			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 },
    			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
    			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
    			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 },
    		};

    		_frames[i]._frameDescriptors = DescriptorAllocatorGrowable{};
    		_frames[i]._frameDescriptors.init(_device, 1000, frame_sizes);
	
    		_mainDeletionQueue.push_function([&, i]() {
    			_frames[i]._frameDescriptors.destroy_pools(_device);
    		});
    }
}

void VulkanEngine::init_pipelines(){
    init_background_pipelines();
    // init_triangle_pipeline();
    init_mesh_pipeline();
    metalRoughMaterial.build_pipelines(this);
    init_shadow_map_pipeline();
    init_skybox_pipeline();
    init_rect_to_cube_pipeline();
    init_default_data();

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
    VkShaderModule gradientShader;
    if (!vkutil::load_shader_module("shaders/gradient_color.comp.spv", _device, &gradientShader))
    {
        fmt::print("Error when building the compute shader \n");
    }

    VkShaderModule skyShader;
    if (!vkutil::load_shader_module("shaders/sky.comp.spv", _device, &skyShader))
    {
        fmt::print("Error when building the compute shader \n");
    }

    VkPipelineShaderStageCreateInfo stageinfo{};
    stageinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageinfo.pNext = nullptr;
    stageinfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageinfo.module = gradientShader;
    //note here is that we are giving
    // it the name of the function we want
    // the shader to use, which is going to be main().
    stageinfo.pName = "main";

    VkComputePipelineCreateInfo computePipelineCreateInfo{};
    computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computePipelineCreateInfo.pNext = nullptr;
    computePipelineCreateInfo.layout = _gradientPipelineLayout;
    computePipelineCreateInfo.stage = stageinfo;


    ComputeEffect gradient;
    gradient.layout = _gradientPipelineLayout;
    gradient.name = "gradient";
    gradient.data = {};

    gradient.data.data1 = glm::vec4(1, 0, 0, 1);
    gradient.data.data2 = glm::vec4(0, 0, 1, 1);

    VK_CHECK(vkCreateComputePipelines(_device,VK_NULL_HANDLE,1,&computePipelineCreateInfo, nullptr, &gradient.pipeline));
    //change the shader module only to create the sky shader
    computePipelineCreateInfo.stage.module = skyShader;

    ComputeEffect sky;
    sky.layout = _gradientPipelineLayout;
    sky.name = "sky";
    sky.data = {};
    //default sky parameters
    sky.data.data1 = glm::vec4(0.1, 0.2, 0.4 ,0.97);

    VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &sky.pipeline));

    backgroundEffects.push_back(gradient);
    backgroundEffects.push_back(sky);


    vkDestroyShaderModule(_device, gradientShader, nullptr);
    vkDestroyShaderModule(_device, skyShader, nullptr);
    
    _mainDeletionQueue.push_function([=]() {
    		vkDestroyPipelineLayout(_device, _gradientPipelineLayout, nullptr);
    		vkDestroyPipeline(_device, gradient.pipeline, nullptr);
    		vkDestroyPipeline(_device, sky.pipeline, nullptr);
		});
}


void VulkanEngine::immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function){
    VK_CHECK(vkResetFences(_device, 1, &_imFence));
    VK_CHECK(vkResetCommandBuffer(_imCommandBuffer, 0));

    VkCommandBuffer cmd = _imCommandBuffer;
    
    VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
    function(cmd);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmdSubmitInfo = vkinit::command_buffer_submit_info(cmd);
    VkSubmitInfo2 submit = vkinit::submit_info(&cmdSubmitInfo, nullptr, nullptr);

    VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, _imFence));
    VK_CHECK(vkWaitForFences(_device, 1, &_imFence, true, 9999999999));

    
}


void VulkanEngine::init_imgui(){
    VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
    { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
    { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
    { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
    { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } };

    VkDescriptorPoolCreateInfo pool_info = {};
    float preFilterRoughness;
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    VkDescriptorPool imguiPool;
    VK_CHECK(vkCreateDescriptorPool(_device, &pool_info, nullptr, &imguiPool));

    ImGui::CreateContext();

    //initializes ImGui for SDL2
    ImGui_ImplSDL2_InitForVulkan(_window);

    //initializing ImGui for Vulkan
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = _instance;
    init_info.PhysicalDevice = _chosenGPU;
    init_info.Device = _device;
    init_info.Queue = _graphicsQueue;
    init_info.DescriptorPool = imguiPool;
    init_info.MinImageCount = 3;
    init_info.ImageCount = 3;
    init_info.UseDynamicRendering = true;

    //dynamic rendering for ImGui
    init_info.PipelineRenderingCreateInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &_swapchainImageFormat;

    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&init_info);

    ImGui_ImplVulkan_CreateFontsTexture();

    _mainDeletionQueue.push_function([=]() {
        ImGui_ImplVulkan_Shutdown();
        vkDestroyDescriptorPool(_device, imguiPool, nullptr);
    });

}


void VulkanEngine::draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView){
    VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingInfo renderInfo = vkinit::rendering_info(_swapchainExtent, &colorAttachment, nullptr);

    vkCmdBeginRendering(cmd, &renderInfo);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

    vkCmdEndRendering(cmd);
}


void VulkanEngine::init_triangle_pipeline(){
    VkShaderModule vertexShader;
    
    if (!vkutil::load_shader_module("shaders/colored_triangle.vert.spv", _device, &vertexShader))
    {
        fmt::print("Error when building the compute shader \n");
    }else{
        fmt::print("Triangle vertex shader loaded\n");
    }

    VkShaderModule fragmentShader;
    if (!vkutil::load_shader_module("shaders/colored_triangle.frag.spv", _device, &fragmentShader))
    {
        fmt::print("Error when building the compute shader \n");
    }else{
        fmt::print("Triangle fragment shader loaded\n");
    }

    VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();
    VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &_trianglePipelineLayout));

    PipelineBuilder pipelineBuilder;
    pipelineBuilder._pipelineLayout = _trianglePipelineLayout;

    pipelineBuilder.set_shaders(vertexShader, fragmentShader);
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.set_multisampling_none();
    pipelineBuilder.disable_blending();
    pipelineBuilder.disable_depth_test();
    pipelineBuilder.set_color_attachment_format(_drawImage.imageFormat);
    pipelineBuilder.set_depth_format(_depthImage.imageFormat); 

    _trianglePipeline = pipelineBuilder.build_pipeline(_device);

    vkDestroyShaderModule(_device, fragmentShader, nullptr);
    vkDestroyShaderModule(_device, vertexShader, nullptr);

    _mainDeletionQueue.push_function([&]() {
        vkDestroyPipelineLayout(_device, _trianglePipelineLayout, nullptr);
        vkDestroyPipeline(_device, _trianglePipeline, nullptr);
    });
}

void VulkanEngine::draw_geometry(VkCommandBuffer cmd){

    std::vector<uint32_t> opaque_draws;
    opaque_draws.reserve(mainDrawContext.OpaqueSurfaces.size());

    for (uint32_t i = 0; i < mainDrawContext.OpaqueSurfaces.size(); i++) {
        if(is_visible(mainDrawContext.OpaqueSurfaces[i], sceneData.viewproj))
            opaque_draws.push_back(i);
    }



    std::sort(opaque_draws.begin(), opaque_draws.end(), [&](const auto& iA, const auto& iB) {
        const RenderObject& A = mainDrawContext.OpaqueSurfaces[iA];
        const RenderObject& B = mainDrawContext.OpaqueSurfaces[iB];
        if (A.material == B.material) {
            return A.indexBuffer < B.indexBuffer;
        }
        else {
            return A.material < B.material;
        }
    });
    
    
    //reset counters
    stats.drawcall_count = 0;
    stats.triangle_count = 0;
    
    auto start = std::chrono::system_clock::now();
    
    //begin a render pass  connected to our draw image
    VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(_drawImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo depthAttachment = vkinit::depth_attachment_info(_depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    VkRenderingInfo renderInfo = vkinit::rendering_info(_drawExtent, &colorAttachment, &depthAttachment);    
    
    vkCmdBeginRendering(cmd, &renderInfo);

    // vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _trianglePipeline);

    //set dynamic viewport and scissor
    VkViewport viewport = {};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = _drawExtent.width;
    viewport.height = _drawExtent.height;
    viewport.minDepth = 0.f;
    viewport.maxDepth = 1.f;

    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = _drawExtent.width;
    scissor.extent.height = _drawExtent.height;

    vkCmdSetScissor(cmd, 0, 1, &scissor);

    //allocate a new uniform buffer for the scene data.
    // It will contain all the information about the scene to
    // be rendered.
    //
    // VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT specifies that the
    // buffer can be used in a VkDescriptorBufferInfo suitable
    // for occupying a VkDescriptorSet slot either of type
    //
    // VMA_MEMORY_USAGE_CPU_TO_GPU specifies that this memory block
    // will be written by the host (CPU) frequently and will be read
    // by the device (GPU).
    AllocatedBuffer gpuSceneDataBuffer = create_buffer(sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    //add it to the deletion queue of this frame so it gets deleted once its been used
    get_current_frame()._deletionQueue.push_function([=, this]() {
        destroy_buffer(gpuSceneDataBuffer);
    });

    AllocatedBuffer gpuShadowBuffer = create_buffer(sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    get_current_frame()._deletionQueue.push_function([=, this]() {
        destroy_buffer(gpuShadowBuffer);
    });
    

    //Here we obtain the mapped data from the AllocatedBuffer gpuSceneDataBuffer and
    // set them to the sceneData which was populated in update_Scene().
    GPUSceneData* sceneUniformData = (GPUSceneData*)gpuSceneDataBuffer.allocation->GetMappedData();
    *sceneUniformData = sceneData;

    //We also do the exact same thing for the shadow map rendering because
    // it uses a different camera and therefore sees different things.
    GPUSceneData* shadowUniformData = (GPUSceneData*)gpuShadowBuffer.allocation->GetMappedData();
    *shadowUniformData = shadowSceneData;

    //create a descriptor set that binds that buffer and update it
    VkDescriptorSet globalDescriptor = get_current_frame()._frameDescriptors.allocate(_device, _gpuSceneDataDescriptorLayout);

    DescriptorWriter writer;
    writer.write_buffer(0, gpuSceneDataBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.write_buffer(1, gpuShadowBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.write_image(2, _lightDepthImage.imageView, _defaultSamplerLinear, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    //skybox cube map texture that gets sent to the main shader for environment mapping
    // replacing with _irradianceMapTexture for PBR
    writer.write_image(3, _cubeMapHDRTexture.imageView, _defaultCubeSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.write_image(4, _irradianceMapTexture.imageView, _defaultCubeSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.write_image(5, _brdfLUTTexture.imageView, _defaultSamplerLinear, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.update_set(_device, globalDescriptor);
    
    
    
  /////////////////////// Code to draw a rectangle ////////////////////
    // //Recatangle
    // vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _meshPipeline);

    // GPUDrawPushConstants push_constants;
    // push_constants.worldMatrix = glm::mat4{ 1.f };
    // push_constants.vertexBuffer = rectangle.vertexBufferAddress;

    // vkCmdPushConstants(cmd, _meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &push_constants);
    // vkCmdBindIndexBuffer(cmd, rectangle.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

    // vkCmdDrawIndexed(cmd, 6, 1, 0, 0, 0);
  ////////////////////////////////////////////////////////////////////////

  

    //skybox
    // vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _meshPipeline);

    // GPUDrawPushConstants push_constants;
    // push_constants.worldMatrix = glm::mat4{ 1.f };
    // push_constants.vertexBuffer = cube.vertexBufferAddress;

    // vkCmdPushConstants(cmd, _meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &push_constants);
    // vkCmdBindIndexBuffer(cmd, cube.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

    // // vkCmdDrawIndexed(cmd, 6, 1, 0, 0, 0);
    // vkCmdDrawIndexed(cmd, 36, 1, 0, 0, 0);
    
    // vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _meshPipeline);

    //bind a texture
    VkDescriptorSet imageSet = get_current_frame()._frameDescriptors.allocate(_device, _singleImageDescriptorLayout);
    {
        DescriptorWriter writer;
        writer.write_image(0, _errorCheckerboardImage.imageView, _defaultSamplerNearest, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

        writer.update_set(_device, imageSet);
    }
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _meshPipelineLayout, 0, 1, &imageSet, 0, nullptr);
   
    // GPUDrawPushConstants push_constants; 
    // push_constants.worldMatrix = glm::mat4{ 1.f };

    // glm::mat4 view = glm::translate(glm::vec3{ 0,0,-5 });

    // // view = glm::translate(glm::vec3{ 0,0,-1});
    // // view *= glm::rotate(glm::mat4(1.f), 90.f, glm::vec3{1, 0, 0});
    // // camera projection
    // glm::mat4 projection = glm::perspective(glm::radians(70.f), (float)_drawExtent.width / (float)_drawExtent.height, 10000.f, 1.f);

    // // invert the Y direction on projection matrix so that we are more similar
    // // to opengl and gltf axis
    // projection[1][1] *= -1;

    // push_constants.worldMatrix = projection * view;

    
    // push_constants.vertexBuffer = testMeshes[2]->meshBuffers.vertexBufferAddress;
    // // fmt::print("test Meshes size {}\n", testMeshes.size());

    // vkCmdPushConstants(cmd, _meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &push_constants);
    // vkCmdBindIndexBuffer(cmd, testMeshes[2]->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

    // vkCmdDrawIndexed(cmd, testMeshes[2]->surfaces[0].count, 1, testMeshes[2]->surfaces[0].startIndex, 0, 0);

    // for (const RenderObject& draw : mainDrawContext.OpaqueSurfaces) {

    //     vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS, draw.material->pipeline->pipeline);
    //     vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,draw.material->pipeline->layout, 0,1, &globalDescriptor,0,nullptr );
    //     vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,draw.material->pipeline->layout, 1,1, &draw.material->materialSet,0,nullptr );

    //     vkCmdBindIndexBuffer(cmd, draw.indexBuffer,0,VK_INDEX_TYPE_UINT32);

    //     GPUDrawPushConstants pushConstants;
    //     pushConstants.vertexBuffer = draw.vertexBufferAddress;
    //     pushConstants.worldMatrix = draw.transform;
    //     vkCmdPushConstants(cmd,draw.material->pipeline->layout ,VK_SHADER_STAGE_VERTEX_BIT,0, sizeof(GPUDrawPushConstants), &pushConstants);

    //     vkCmdDrawIndexed(cmd,draw.indexCount,1,draw.firstIndex,0,0);y
    // }


    
//keeping track of the state of the draw pipeline so repeats don't happen 
    MaterialPipeline* lastPipeline = nullptr;
    MaterialInstance* lastMaterial = nullptr;
    VkBuffer lastIndexBuffer = VK_NULL_HANDLE;

    auto draw = [&](const RenderObject& draw){

        if(draw.material != lastMaterial){
            lastMaterial = draw.material;
            if(draw.material->pipeline != lastPipeline){
                lastPipeline = draw.material->pipeline;
                vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS, draw.material->pipeline->pipeline);
                vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,draw.material->pipeline->layout, 0,1, &globalDescriptor,0,nullptr );

                VkViewport viewport = {};
                viewport.x = 0;
                viewport.y = 0;
                viewport.width = (float)_windowExtent.width;
                viewport.height = (float)_windowExtent.height;
                viewport.minDepth = 0.f;
                viewport.maxDepth = 1.f;

                vkCmdSetViewport(cmd, 0, 1, &viewport);

                VkRect2D scissor = {};
                scissor.offset.x = 0;
                scissor.offset.y = 0;
                scissor.extent.width = _windowExtent.width;
                scissor.extent.height = _windowExtent.height;

                vkCmdSetScissor(cmd, 0, 1, &scissor);
            }
        
            vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,draw.material->pipeline->layout, 1,1, &draw.material->materialSet,0,nullptr );
        }

        if(draw.indexBuffer != lastIndexBuffer){
            lastIndexBuffer = draw.indexBuffer;
            
            vkCmdBindIndexBuffer(cmd, draw.indexBuffer,0,VK_INDEX_TYPE_UINT32);
        }


        GPUDrawPushConstants pushConstants;
        pushConstants.vertexBuffer = draw.vertexBufferAddress;
        pushConstants.worldMatrix = draw.transform;
        pushConstants.useNormal = draw.useNormal;
        pushConstants.useMetalTex = draw.useMetalTex;
        pushConstants.useAOTex = draw.useAOTex;
        pushConstants.useORM = draw.useORM;
        pushConstants.useEmissionTex = draw.useEmissionTex;
        
        vkCmdPushConstants(cmd,draw.material->pipeline->layout ,VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GPUDrawPushConstants), &pushConstants);
        // vkCmdPushConstants(cmd, draw.material->pipeline->layout, VK_SHADER_STAGE_FRAGMENT_BIT,sizeof(GPUDrawPushConstants), sizeof(int), &draw.useNormal);

        vkCmdDrawIndexed(cmd,draw.indexCount,1,draw.firstIndex,0,0);

        stats.drawcall_count++;
        stats.triangle_count += draw.indexCount / 3;   
    };

    render_skybox(cmd, renderInfo);

    for (auto& r : opaque_draws) {
        draw(mainDrawContext.OpaqueSurfaces[r]);
    }

    for (auto& r : mainDrawContext.TransparentSurfaces) {
        draw(r);
    }

    //render skybox at the end to not redraw the fragment shader pixel.
    vkCmdEndRendering(cmd);

    auto end = std::chrono::system_clock::now();

    //convert to microseconds (integer), and then come back to miliseconds
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    stats.mesh_draw_time = elapsed.count() / 1000.f;

}

//This function does all of the boilerplate code for creating a buffer.
AllocatedBuffer VulkanEngine::create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage){
    // allocate buffer
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


void VulkanEngine::destroy_buffer(const AllocatedBuffer& buffer)
{
    vmaDestroyBuffer(_allocator, buffer.buffer, buffer.allocation);
}


GPUMeshBuffers VulkanEngine::upload_mesh(std::span<uint32_t> indices, std::span<Vertex> vertices){
    //convert to Bytes
    const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
    const size_t indexBufferSize = indices.size() * sizeof(uint32_t);
    GPUMeshBuffers newSurface;

    //create vertex buffer
    newSurface.vertexBuffer = create_buffer(vertexBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY);

    //find the adress of the vertex buffer
    VkBufferDeviceAddressInfo deviceAdressInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,.buffer = newSurface.vertexBuffer.buffer };
    newSurface.vertexBufferAddress = vkGetBufferDeviceAddress(_device, &deviceAdressInfo);

    //create index buffer
    newSurface.indexBuffer = create_buffer(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY);

    AllocatedBuffer staging = create_buffer(vertexBufferSize + indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    void* data = staging.allocation->GetMappedData();

    // copy vertex buffer
    memcpy(data, vertices.data(), vertexBufferSize);
    // copy index buffer
    memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize);
    
    immediate_submit([&](VkCommandBuffer cmd){
        VkBufferCopy vertexCopy{ 0 };
        vertexCopy.dstOffset = 0;
        vertexCopy.srcOffset = 0;
        vertexCopy.size = vertexBufferSize;

        vkCmdCopyBuffer(cmd, staging.buffer, newSurface.vertexBuffer.buffer, 1, &vertexCopy);

        VkBufferCopy indexCopy{ 0 };
        indexCopy.dstOffset = 0;
        indexCopy.srcOffset = vertexBufferSize;
        indexCopy.size = indexBufferSize;

        vkCmdCopyBuffer(cmd, staging.buffer, newSurface.indexBuffer.buffer, 1, &indexCopy);
    });

	destroy_buffer(staging);

	return newSurface;

}

void VulkanEngine::init_mesh_pipeline(){
    VkShaderModule triangleFragShader;
    if (!vkutil::load_shader_module("shaders/tex_image.frag.spv", _device, &triangleFragShader)) {
        fmt::print("Error when building the triangle fragment shader module");
    }
    else {
        fmt::print("Triangle fragment shader succesfully loaded\n");
    }

    VkShaderModule triangleVertexShader;
    if (!vkutil::load_shader_module("shaders/colored_triangle_mesh.vert.spv", _device, &triangleVertexShader)) {
        fmt::print("Error when building the triangle vertex shader module");
    }
    else {
        fmt::print("Triangle vertex shader succesfully loaded\n");
    }

    VkPushConstantRange bufferRange{};
    bufferRange.offset = 0;
    bufferRange.size = sizeof(GPUDrawPushConstants);
    bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    // VkPushConstantRange normalRange{};
    // normalRange.offset = sizeof(GPUDrawPushConstants);
    // normalRange.size = sizeof(int);
    // normalRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // VkPushConstantRange pushConstants[] = {bufferRange, normalRange};
    
    VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();
    pipeline_layout_info.pPushConstantRanges = &bufferRange;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pSetLayouts = &_singleImageDescriptorLayout;
    pipeline_layout_info.setLayoutCount = 1;

    VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &_meshPipelineLayout));
    PipelineBuilder pipelineBuilder;

    //use the triangle layout we created
    pipelineBuilder._pipelineLayout = _meshPipelineLayout;
    //connecting the vertex and pixel shaders to the pipeline
    pipelineBuilder.set_shaders(triangleVertexShader, triangleFragShader);
    //it will draw triangles
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    //filled triangles
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    //no backface culling
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    //no multisampling
    pipelineBuilder.set_multisampling_none();
    
    //no blending
    pipelineBuilder.disable_blending();

    //blending by adding colors together
    // pipelineBuilder.enable_blending_additive();

    // pipelineBuilder.disable_depth_test();
    pipelineBuilder.enable_depth_test(true, VK_COMPARE_OP_GREATER_OR_EQUAL);

    //connect the image format we will draw into, from draw image
    pipelineBuilder.set_color_attachment_format(_drawImage.imageFormat);
    pipelineBuilder.set_depth_format(_depthImage.imageFormat);

    //finally build the pipeline
    _meshPipeline = pipelineBuilder.build_pipeline(_device);

    //clean structures
    vkDestroyShaderModule(_device, triangleFragShader, nullptr);
    vkDestroyShaderModule(_device, triangleVertexShader, nullptr);

    _mainDeletionQueue.push_function([&]() {
        vkDestroyPipelineLayout(_device, _meshPipelineLayout, nullptr);
        vkDestroyPipeline(_device, _meshPipeline, nullptr);
    });
}

void VulkanEngine::init_default_data() {
    load_cube_map();
    std::array<Vertex,4> rect_vertices;

    rect_vertices[0].position = {0.5,-0.5, 0};
    rect_vertices[1].position = {0.5,0.5, 0};
    rect_vertices[2].position = {-0.5,-0.5, 0};
    rect_vertices[3].position = {-0.5,0.5, 0};

    rect_vertices[0].color = {0,0, 0,1};
    rect_vertices[1].color = { 0.5,0.5,0.5 ,1};
    rect_vertices[2].color = { 1,0, 0,1 };
    rect_vertices[3].color = { 0,1, 0,1 };

    std::array<uint32_t,6> rect_indices;

    rect_indices[0] = 0;
    rect_indices[1] = 1;
    rect_indices[2] = 2;

    rect_indices[3] = 2;
    rect_indices[4] = 1;
    rect_indices[5] = 3;

    rectangle = upload_mesh(rect_indices,rect_vertices);
    std::array<Vertex, 8> cube_vertices;


    /// the cube position and index buffer was generated using
    // gemini because I didn't feel like doing this.
    // Front Face Nodes
    cube_vertices[0].position = {-0.5, -0.5,  0.5}; // Bottom Left
    cube_vertices[1].position = { 0.5, -0.5,  0.5}; // Bottom Right
    cube_vertices[2].position = { 0.5,  0.5,  0.5}; // Top Right
    cube_vertices[3].position = {-0.5,  0.5,  0.5}; // Top Left

    // Back Face Nodes
    cube_vertices[4].position = {-0.5, -0.5, -0.5}; // Bottom Left
    cube_vertices[5].position = { 0.5, -0.5, -0.5}; // Bottom Right
    cube_vertices[6].position = { 0.5,  0.5, -0.5}; // Top Right
    cube_vertices[7].position = {-0.5,  0.5, -0.5}; // Top Left

    // Colors (White for visibility, or easily changeable)
    cube_vertices[0].color = { 1, 1, 1, 1 };
    cube_vertices[1].color = { 1, 1, 1, 1 };
    cube_vertices[2].color = { 1, 1, 1, 1 };
    cube_vertices[3].color = { 1, 1, 1, 1 };
    cube_vertices[4].color = { 1, 1, 1, 1 };
    cube_vertices[5].color = { 1, 1, 1, 1 };
    cube_vertices[6].color = { 1, 1, 1, 1 };
    cube_vertices[7].color = { 1, 1, 1, 1 };

    std::array<uint32_t, 36> cube_indices;

    // Front Face
    cube_indices[0]  = 0;
    cube_indices[1]  = 1;
    cube_indices[2]  = 2;
    cube_indices[3]  = 2;
    cube_indices[4]  = 3;
    cube_indices[5]  = 0;

    // Right Face
    cube_indices[6]  = 1;
    cube_indices[7]  = 5;
    cube_indices[8]  = 6;
    cube_indices[9]  = 6;
    cube_indices[10] = 2;
    cube_indices[11] = 1;

    // Back Face
    cube_indices[12] = 5;
    cube_indices[13] = 4;
    cube_indices[14] = 7;
    cube_indices[15] = 7;
    cube_indices[16] = 6;
    cube_indices[17] = 5;

    // Left Face
    cube_indices[18] = 4;
    cube_indices[19] = 0;
    cube_indices[20] = 3;
    cube_indices[21] = 3;
    cube_indices[22] = 7;
    cube_indices[23] = 4;

    // Top Face
    cube_indices[24] = 3;
    cube_indices[25] = 2;
    cube_indices[26] = 6;
    cube_indices[27] = 6;
    cube_indices[28] = 7;
    cube_indices[29] = 3;

    // Bottom Face
    cube_indices[30] = 4;
    cube_indices[31] = 5;
    cube_indices[32] = 1;
    cube_indices[33] = 1;
    cube_indices[34] = 0;
    cube_indices[35] = 4;

    cube = upload_mesh(cube_indices, cube_vertices);

    //delete the rectangle data on engine shutdown
    _mainDeletionQueue.push_function([&](){
        destroy_buffer(rectangle.indexBuffer);
        destroy_buffer(rectangle.vertexBuffer);
        destroy_buffer(cube.indexBuffer);
        destroy_buffer(cube.vertexBuffer);
    });

    testMeshes = loadGltfMeshes(this,"assets/basicmesh.glb").value();

    //default textures, white, grey, black. 1 pixel each
    uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
    _whiteImage = create_image((void*)&white, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
    VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_VIEW_TYPE_2D);

    uint32_t grey = glm::packUnorm4x8(glm::vec4(0.66f, 0.66f, 0.66f, 1));
    _greyImage = create_image((void*)&grey, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
    VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_VIEW_TYPE_2D);

    uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 0));
    _blackImage = create_image((void*)&black, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
    VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_VIEW_TYPE_2D);

    //checkerboard image
    uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
    std::array<uint32_t, 16 *16 > pixels; //for 16x16 checkerboard texture
    for (int x = 0; x < 16; x++) {
        for (int y = 0; y < 16; y++) {
        	pixels[y*16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
        }
    }
    _errorCheckerboardImage = create_image(pixels.data(), VkExtent3D{16, 16, 1}, VK_FORMAT_R8G8B8A8_UNORM,
    VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_VIEW_TYPE_2D);

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

    GLTFMetallic_Roughness::MaterialResources materialResources;
    //default the material textures
    int width, height, channels;
    
    // std::string gravelPath = {};
    ////////loading default textures for normal mapping
    
    //flip vertically for some reason gets the texture right?????///// I don't get
    // I found this due to a bug that made the gravel texture look more like gravel.
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load("assets/Gravel_001_BaseColor.jpg", &width, &height, &channels, 4);
    VkExtent3D size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    
    _gravelImage = create_image(data, size, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_VIEW_TYPE_2D, false);

    stbi_image_free(data);

    data = stbi_load("assets/Gravel_001_Roughness.jpg", &width, &height, &channels, 4);
    size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};

    _gravelRoughness = create_image(data, size, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_VIEW_TYPE_2D, false);
    stbi_image_free(data);

    data = stbi_load("assets/Gravel_001_Normal.jpg", &width, &height, &channels, 4);
    size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};

    _gravelNormal = create_image(data, size, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_VIEW_TYPE_2D, false);
    stbi_image_free(data);
    stbi_set_flip_vertically_on_load(false);
    
    /////////
    _mainDeletionQueue.push_function([&](){
        vkDestroySampler(_device,_defaultSamplerNearest,nullptr);
        vkDestroySampler(_device,_defaultSamplerLinear,nullptr);
        vkDestroySampler(_device, _defaultCubeSampler, nullptr);

        destroy_image(_whiteImage);
        destroy_image(_greyImage);
        destroy_image(_blackImage);
        destroy_image(_errorCheckerboardImage);
        destroy_image(_gravelImage);
        destroy_image(_gravelRoughness);
        destroy_image(_gravelNormal);
        destroy_image(_cubeMapTextures);
        destroy_image(_cubeMapHDRTexture);
        destroy_image(_loadedHDRTexture);
        destroy_image(_irradianceMapTexture);
        destroy_image(_brdfLUTTexture);
    });
    
    materialResources.colorImage = _gravelImage;
    materialResources.colorSampler = _defaultSamplerLinear;
    materialResources.metalRoughImage = _gravelRoughness;
    materialResources.metalRoughSampler = _defaultSamplerLinear;
    materialResources.normalImage = _gravelNormal;
    materialResources.normalSampler = _defaultSamplerLinear;
    materialResources.ambientImage = _whiteImage;
    materialResources.ambientSampler = _defaultSamplerLinear;
    materialResources.emissiveImage = _whiteImage;
    materialResources.emissiveSampler = _defaultSamplerLinear;

    //set the uniform buffer for the material data
    materialConstants = create_buffer(sizeof(GLTFMetallic_Roughness::MaterialConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    //write the buffer
    GLTFMetallic_Roughness::MaterialConstants* sceneUniformData = (GLTFMetallic_Roughness::MaterialConstants*)materialConstants.allocation->GetMappedData();
    sceneUniformData->colorFactors = colorFactors;
    sceneUniformData->metal_rough_factors = metal_rough_factors;
    sceneUniformData->emissive_factors = glm::vec4(0.0);
    
    _mainDeletionQueue.push_function([=, this]() {
        destroy_buffer(materialConstants);
    });

    materialResources.dataBuffer = materialConstants.buffer;
    materialResources.dataBufferOffset = 0;

    defaultData = metalRoughMaterial.write_material(_device,MaterialPass::MainColor,materialResources, globalDescriptorAllocator);
    
    _mainDeletionQueue.push_function([=, this]() {
        metalRoughMaterial.clear_resources(_device);
    });

    for (auto& m : testMeshes) {
        std::shared_ptr<MeshNode> newNode = std::make_shared<MeshNode>();
        newNode->mesh = m;

        newNode->localTransform = glm::mat4{ 1.f };
        newNode->worldTransform = glm::mat4{ 1.f };

        for (auto& s : newNode->mesh->surfaces) {
        	s.material = std::make_shared<GLTFMaterial>(defaultData);
        }

        loadedNodes[m->name] = std::move(newNode);
    }

    convert_to_cube();
}

AllocatedImage VulkanEngine::create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, VkImageViewType viewType, bool mipmapped){
    AllocatedImage newImage;
    newImage.imageFormat = format;
    newImage.imageExtent = size;

    VkImageCreateInfo img_info = vkinit::image_create_info(format, usage, size);
    if (mipmapped) {
        img_info.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(size.width, size.height)))) + 1;
    }

    if (viewType == VK_IMAGE_VIEW_TYPE_CUBE) {
        img_info.arrayLayers = 6;
        img_info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }
    // always allocate images on dedicated GPU memory
    VmaAllocationCreateInfo allocinfo = {};
    allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // allocate and create the image
    VK_CHECK(vmaCreateImage(_allocator, &img_info, &allocinfo, &newImage.image, &newImage.allocation, nullptr));
    // if the format is a depth format, we will need to have it use the correct
    // aspect flag
    VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;
    if (format == VK_FORMAT_D32_SFLOAT) {
        aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;
    }

    // build a image-view for the image
    VkImageViewCreateInfo view_info = vkinit::imageview_create_info(format, newImage.image, aspectFlag, viewType);
    view_info.subresourceRange.levelCount = img_info.mipLevels;

    if (viewType == VK_IMAGE_VIEW_TYPE_CUBE) {
        view_info.subresourceRange.layerCount = 6;
    }

    VK_CHECK(vkCreateImageView(_device, &view_info, nullptr, &newImage.imageView));

    return newImage;
}

AllocatedImage VulkanEngine::create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, VkImageViewType viewType, bool mipmapped){
    int pixelSize = 4; // Default for RGBA8
    if (format == VK_FORMAT_R32G32B32A32_SFLOAT) {
        pixelSize = 16; // 4 channels * 4 bytes (float)
    }
    size_t data_size = size.depth * size.width * size.height * pixelSize;
    AllocatedBuffer uploadbuffer = create_buffer(data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    memcpy(uploadbuffer.info.pMappedData, data, data_size);

    AllocatedImage new_image = create_image(size, format, usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, viewType, mipmapped);

    immediate_submit([&](VkCommandBuffer cmd) {
    		vkutil::transition_image(cmd, new_image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    		VkBufferImageCopy copyRegion = {};
    		copyRegion.bufferOffset = 0;
    		copyRegion.bufferRowLength = 0;
    		copyRegion.bufferImageHeight = 0;

    		copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    		copyRegion.imageSubresource.mipLevel = 0;
    		copyRegion.imageSubresource.baseArrayLayer = 0;
    		copyRegion.imageSubresource.layerCount = 1;
    		copyRegion.imageExtent = size;

    		// copy the buffer into the image
    		vkCmdCopyBufferToImage(cmd, uploadbuffer.buffer, new_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
    			&copyRegion);

    		if(mipmapped){
            vkutil::generate_mipmaps(cmd, new_image.image,VkExtent2D{new_image.imageExtent.width,new_image.imageExtent.height},
                                     (viewType == VK_IMAGE_VIEW_TYPE_CUBE? 6 : 1));
            
    		}else{
		    
            vkutil::transition_image(cmd, new_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    		}
    });
    

	destroy_buffer(uploadbuffer);

	return new_image;
}


void VulkanEngine::destroy_image(const AllocatedImage& img){
    vkDestroyImageView(_device, img.imageView, nullptr);
    vmaDestroyImage(_allocator, img.image, img.allocation);
}


void GLTFMetallic_Roughness::build_pipelines(VulkanEngine* engine){
    // VkShaderModule meshFragShader;
    // if (!vkutil::load_shader_module("shaders/mesh_phong.frag.spv", engine->_device, &meshFragShader)) {
    //     fmt::println("Error when building the triangle fragment shader module");
    // }

    // VkShaderModule meshVertexShader;
    // if (!vkutil::load_shader_module("shaders/mesh_phong.vert.spv", engine->_device, &meshVertexShader)) {
    //     fmt::println("Error when building the triangle vertex shader module");
    // }
    VkShaderModule meshFragShader;
    if (!vkutil::load_shader_module("shaders/mesh_normal.frag.spv", engine->_device, &meshFragShader)) {
        fmt::println("Error when building the triangle fragment shader module\n");
    }

    VkShaderModule meshVertexShader;
    if (!vkutil::load_shader_module("shaders/mesh_normal.vert.spv", engine->_device, &meshVertexShader)) {
        fmt::println("Error when building the triangle vertex shader module\n");
    }

    VkPushConstantRange matrixRange{};
    matrixRange.offset = 0;
    matrixRange.size = sizeof(GPUDrawPushConstants);
    matrixRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // VkPushConstantRange normalRange{};
    // normalRange.offset = sizeof(GPUDrawPushConstants);// the offset must begin where the other one ends.
    // normalRange.size = sizeof(int);
    // normalRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    DescriptorLayoutBuilder builder;
    builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    builder.add_binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    builder.add_binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    builder.add_binding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    builder.add_binding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    builder.add_binding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    //taking advantage of the build pipeline function for the cubeMap.
    // builder.add_binding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    
    materialLayout = builder.build(engine->_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    VkDescriptorSetLayout layouts[] = { engine->_gpuSceneDataDescriptorLayout, materialLayout };

    // VkPushConstantRange pushConstantRanges[] = { matrixRange, normalRange };

    VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();
    pipeline_layout_info.pPushConstantRanges = &matrixRange;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pSetLayouts = layouts;
    pipeline_layout_info.setLayoutCount = 2; //creating 2 pipelines, one Opaque and one 

    VkPipelineLayout newLayout;
    VK_CHECK(vkCreatePipelineLayout(engine->_device, &pipeline_layout_info, nullptr, &newLayout));

    opaquePipeline.layout = newLayout;
    transparentPipeline.layout = newLayout;

    PipelineBuilder pipelineBuilder;
    
    pipelineBuilder._pipelineLayout = newLayout;
    pipelineBuilder.set_shaders(meshVertexShader, meshFragShader);
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST); 
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.set_multisampling_none();
    pipelineBuilder.disable_blending();
    pipelineBuilder.enable_depth_test(true, VK_COMPARE_OP_GREATER_OR_EQUAL);

    pipelineBuilder.set_color_attachment_format(engine->_drawImage.imageFormat);
    pipelineBuilder.set_depth_format(engine->_depthImage.imageFormat);

    opaquePipeline.pipeline = pipelineBuilder.build_pipeline(engine->_device);

    pipelineBuilder.enable_blending_additive();
    pipelineBuilder.enable_depth_test(false, VK_COMPARE_OP_GREATER_OR_EQUAL);

    transparentPipeline.pipeline = pipelineBuilder.build_pipeline(engine->_device);

    vkDestroyShaderModule(engine->_device, meshFragShader, nullptr);
    vkDestroyShaderModule(engine->_device, meshVertexShader, nullptr);

}

MaterialInstance GLTFMetallic_Roughness::write_material(VkDevice device, MaterialPass pass, const MaterialResources& resources, DescriptorAllocatorGrowable& descriptorAllocator){
    MaterialInstance matData;
    matData.passType = pass;
    if (pass == MaterialPass::Transparent) {
        matData.pipeline = &transparentPipeline;
    }else{
        matData.pipeline = &opaquePipeline;
    }

    matData.materialSet = descriptorAllocator.allocate(device, materialLayout);
    writer.clear();
    writer.write_buffer(0, resources.dataBuffer, sizeof(MaterialConstants), resources.dataBufferOffset, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.write_image(1, resources.colorImage.imageView, resources.colorSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.write_image(2, resources.metalRoughImage.imageView, resources.metalRoughSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.write_image(3, resources.normalImage.imageView, resources.normalSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.write_image(4, resources.ambientImage.imageView, resources.ambientSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.write_image(5, resources.emissiveImage.imageView, resources.emissiveSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    
    writer.update_set(device, matData.materialSet);

    return matData;
}

void GLTFMetallic_Roughness::clear_resources(VkDevice device){
    vkDestroyDescriptorSetLayout(device, materialLayout, nullptr);
    vkDestroyPipeline(device, transparentPipeline.pipeline, nullptr);

    vkDestroyPipeline(device, opaquePipeline.pipeline, nullptr);
    //transparentPipeline and OpaquePipeline share the same layout
    // so only one of the layouts needs to be destroyed
    if(transparentPipeline.layout != VK_NULL_HANDLE){
        vkDestroyPipelineLayout(device, transparentPipeline.layout, nullptr);
    }
    
}


void MeshNode::Draw(const glm::mat4& topMatrix, DrawContext& ctx){

    glm::mat4 nodeMatrix = topMatrix * worldTransform;
    for (auto& s : mesh->surfaces) {
        RenderObject def;
        def.indexCount = s.count;
        def.firstIndex = s.startIndex;
        def.indexBuffer = mesh->meshBuffers.indexBuffer.buffer;
        def.material = &s.material->data;
        def.bounds = s.bounds;
        def.transform = nodeMatrix;
        def.vertexBufferAddress = mesh->meshBuffers.vertexBufferAddress;
        def.useNormal = useNormal;
        def.useMetalTex = useMetalTex;
        def.useAOTex = useAOTex;
        def.useORM = useORM;
        def.useEmissionTex = useEmissionTex;
        // fmt::println("entered here {}", useAOTex);

        if(s.material->data.passType == MaterialPass::Transparent){
            ctx.TransparentSurfaces.push_back(def);
        }else{
            
            ctx.OpaqueSurfaces.push_back(def);
        }
    }

    Node::Draw(nodeMatrix, ctx);
}

void VulkanEngine::update_scene(){
    
    mainCamera.update();
    lightCamera.update();
    lightCamera.position = 10.f * sceneData.sunlightDirection;
    
    
    mainDrawContext.OpaqueSurfaces.clear();
    mainDrawContext.TransparentSurfaces.clear();
    lightDrawContext.OpaqueSurfaces.clear();
    lightDrawContext.TransparentSurfaces.clear();

    int useNormal = 1;
    int useMetalTex = 0;
    int useAOTex = 0;
    int useORM = 0;
    int useEmissionTex = 0;
    loadedNodes["Suzanne"]->useNormal = useNormal;
    loadedNodes["Suzanne"]->useMetalTex = useMetalTex;
    loadedNodes["Suzanne"]->useAOTex = useAOTex;
    loadedNodes["Suzanne"]->useORM = useORM;
    loadedNodes["Suzanne"]->useEmissionTex = useEmissionTex;

    // fmt::print("suzanne normal : {}\n", loadedNodes["Suzanne"]->useNormal);
    loadedNodes["Suzanne"]->Draw(glm::mat4{1.f}, mainDrawContext);

    loadedNodes["Suzanne"]->Draw(glm::mat4{1.f}, lightDrawContext);

    // sceneData.view = glm::translate(glm::vec3{ 0,0,-5 });
    sceneData.view = mainCamera.getViewMatrix();
    // camera projection
    sceneData.proj = glm::perspective(glm::radians(70.f), (float)_windowExtent.width / (float)_windowExtent.height, 10000.f, 0.1f);

    sceneData.cameraPosition = glm::vec4(mainCamera.position, 1.f);

    // invert the Y direction on projection matrix so that we are more similar
    // to opengl and gltf axis
    sceneData.proj[1][1] *= -1;
    sceneData.viewproj = sceneData.proj * sceneData.view;

    shadowSceneData.view = glm::lookAt(lightCamera.position, glm::vec3(0.f,0.f,0.f), glm::vec3(0.f,1.f,0.f));
    
    //temporary hardcoded variables for projection size.
    // Cascading shadow map will be implemented in the future to
    // accomodate for larger scenes.
    float sizew = 10.f;
    float sizeh = 10.f;
    // float sizew = (float)_windowExtent.width/2.f;
    // float sizeh = (float)_windowExtent.height/2.f;
    // shadowSceneData.proj = glm::ortho(-sizew, sizew, -sizeh, sizeh, 10000.f, 0.1f);
    shadowSceneData.proj = glm::ortho(-sizew, sizew, -sizeh, sizeh, 1000.f, 0.1f);
    
    shadowSceneData.proj[1][1] *= -1;
    glm::vec3 scaleVector = glm::vec3(0.5f, 0.5f, 1.f);
    glm::mat4 scaleMatrix = glm::scale(glm::mat4{ 1.f }, scaleVector);
    glm::mat4 translateMatrix = glm::translate(glm::mat4{ 1.f }, glm::vec3(0.5f, 0.5f, 0.f));

    
    shadowSceneData.viewproj = translateMatrix * scaleMatrix * shadowSceneData.proj * shadowSceneData.view;

    
    
    //some default lighting parameters
    // sceneData.ambientColor = glm::vec4(0.3f);
    // sceneData.sunlightColor = glm::vec4(1.f, 1.f, 1.f, 1.f);
    // sceneData.sunlightDirection = glm::vec4(0,1,0.5,1.f);
 

    // for (int x = -2; x < 4; x++){
    glm::mat4 scale = glm::scale(glm::vec3{5.0, 0.2, 5.0});
    glm::mat4 translation =  glm::translate(glm::vec3{0, -2, 0});

    loadedNodes["Cube"]->useNormal = useNormal;
    loadedNodes["Cube"]->useMetalTex = useMetalTex;
    loadedNodes["Cube"]->useAOTex = useAOTex;
    loadedNodes["Cube"]->useORM = useORM;
    loadedNodes["Cube"]->useEmissionTex = useEmissionTex;
    loadedNodes["Cube"]->Draw(translation * scale, mainDrawContext);
    loadedNodes["Cube"]->Draw(translation * scale, lightDrawContext);
        
    translation = glm::translate(glm::vec3(0.2, -0.9, 1));
    
    loadedScenes["blowDart"]->Draw(translation, mainDrawContext);
    loadedScenes["blowDart"]->Draw(translation, lightDrawContext);
    
    translation = glm::translate(glm::vec3(10.0, 0, 1));
    loadedScenes["helmet"]->Draw(translation, mainDrawContext);
    loadedScenes["helmet"]->Draw(translation, lightDrawContext);


    // }
    scale = glm::scale(glm::vec3{0.01, 0.01, 0.01});
    for (int x = -2; x < 4; x++){
        translation =  glm::translate(glm::vec3{x, -3, 3});
        loadedScenes["balloon"]->Draw(translation * scale, mainDrawContext);
        loadedScenes["balloon"]->Draw(translation * scale, lightDrawContext);
   
    }
    
    glm::mat4 translate = glm::translate(glm::mat4{ 1.f }, glm::vec3(200, 0, 0)); 
    glm::mat4 smallTranslate = glm::translate(glm::mat4{ 1.f }, glm::vec3(1, -1.5, 0)); 
    scale = glm::scale(glm::vec3{5.0, 5.0, 5.0});

    // loadedScenes
    // loadedScenes["structure"]->Draw(glm::mat4{ 1.f }, mainDrawContext);
    loadedScenes["virtual city"]->Draw(translate, mainDrawContext);
    loadedScenes["donut"]->Draw(smallTranslate * scale, mainDrawContext);
    
    // loadedScenes["structure"]->Draw(glm::mat4{ 1.f }, lightDrawContext);
    loadedScenes["virtual city"]->Draw(translate, lightDrawContext);
    loadedScenes["donut"]->Draw(smallTranslate * scale, lightDrawContext);
}
 
void VulkanEngine::init_shadow_map_pipeline(){
        
    // VkShaderModule shadowFragShader;
    // if (!vkutil::load_shader_module("shaders/mesh_phong.frag.spv", engine->_device, &meshFragShader)) {
    //     fmt::println("Error when building the triangle fragment shader module");
    // }


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

    //delete the pipeline and pipelin layout
    // almost forgot on previous runs. Validation layers not happy :(
    _mainDeletionQueue.push_function([&](){
        vkDestroyPipeline(_device, _shadowPipeline, nullptr);
        vkDestroyPipelineLayout(_device, _shadowPipelineLayout, nullptr);                                 
     });
    
}


void VulkanEngine::render_shadow_map(VkCommandBuffer cmd){

    std::vector<uint32_t> light_opaque_draws;
    light_opaque_draws.reserve(lightDrawContext.OpaqueSurfaces.size());
   
    // glm::mat4 lightproj = glm::ortho(-(float)(2 * _windowExtent.width), (float)(2 * _windowExtent.width), -(float)(2 * _windowExtent.height), (float)(2 * _windowExtent.height), 10000.f, 0.1f); 
    // float sizew = (float)_windowExtent.width/2.f;
    // float sizeh = (float)_windowExtent.height/2.f;
    // glm::mat4 lightproj = glm::ortho(-sizew, sizew, -sizeh, sizeh, 10000.f, 0.1f);
    // lightproj[1][1] *= -1;

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

    get_current_frame()._deletionQueue.push_function([=, this]() {
        destroy_buffer(gpuSceneDataBuffer);
    });

    GPUSceneData* sceneUniformData = (GPUSceneData*)gpuSceneDataBuffer.allocation->GetMappedData();
    *sceneUniformData = shadowSceneData;
    
    VkDescriptorSet globalDescriptor = get_current_frame()._frameDescriptors.allocate(_device, _shadowSceneDataDescriptorLayout);
    
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

void VulkanEngine::load_cube_map(){

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
    AllocatedBuffer uploadbuffer = create_buffer(data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    memcpy(uploadbuffer.info.pMappedData, textures.data(), data_size);

    AllocatedImage new_image = create_image(size, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_VIEW_TYPE_CUBE, false);


    immediate_submit([&](VkCommandBuffer cmd) {
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


    destroy_buffer(uploadbuffer);

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
        AllocatedImage newer_image= create_image(data, size, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_VIEW_TYPE_2D, false);        

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

        _cubeMapHDRTexture = create_image(cubemapSize, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_VIEW_TYPE_CUBE, false);
        _irradianceMapTexture = create_image(irradianceMapSize, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_VIEW_TYPE_CUBE,false);
        _brdfLUTTexture = create_image(cubemapSize, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_VIEW_TYPE_2D,false);
        
        
    }
    else
    {
        fmt::println("failed to load hdr");        
    }  

       
}


void VulkanEngine::init_skybox_pipeline(){
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

    //delete the pipeline and pipelin layout
    // almost forgot on previous runs. Validation layers not happy :(
    _mainDeletionQueue.push_function([&](){
        vkDestroyPipeline(_device, _skyboxPipeline, nullptr);
        vkDestroyPipelineLayout(_device, _skyboxPipelineLayout, nullptr);                                 
     });
    
}


void VulkanEngine::render_skybox(VkCommandBuffer cmd, VkRenderingInfo& renderInfo){
    //Bind Pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _skyboxPipeline);

    VkDescriptorSet globalDescriptor = get_current_frame()._frameDescriptors.allocate(_device, _gpuSceneDataDescriptorLayout);
    
    DescriptorWriter writer;
    AllocatedBuffer gpuSceneDataBuffer = create_buffer(sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    GPUSceneData* sceneUniformData = (GPUSceneData*)gpuSceneDataBuffer.allocation->GetMappedData();
    *sceneUniformData = sceneData; // Copy current scene data

    get_current_frame()._deletionQueue.push_function([=, this]() {
        destroy_buffer(gpuSceneDataBuffer);
    });

    writer.write_buffer(0, gpuSceneDataBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    //skybox cube map texture that gets sent to the skybox shader to render the skybox
    //writer.write_image(3, _cubeMapTextures.imageView, _defaultCubeSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.write_image(3, _cubeMapHDRTexture.imageView, _defaultCubeSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.write_image(4, _irradianceMapTexture.imageView, _defaultCubeSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.update_set(_device, globalDescriptor);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _skyboxPipelineLayout, 0, 1, &globalDescriptor, 0, nullptr);

    vkCmdBindIndexBuffer(cmd, cube.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

    GPUDrawPushConstants pushConstants;
    pushConstants.vertexBuffer = cube.vertexBufferAddress;
    
    // This effectively removes the translation from the view matrix
    pushConstants.worldMatrix = glm::translate(glm::mat4{ 1.f }, mainCamera.position);

    vkCmdPushConstants(cmd, _skyboxPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GPUDrawPushConstants), &pushConstants);

    vkCmdDrawIndexed(cmd, 36, 1, 0, 0, 0);
    
 
}

void VulkanEngine::init_rect_to_cube_pipeline(){
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
    
    _mainDeletionQueue.push_function([=]() {
    		vkDestroyPipelineLayout(_device, _rectToCubePipelineLayout, nullptr);
    		vkDestroyPipelineLayout(_device, _brdfLUTPipelineLayout, nullptr);
    		vkDestroyPipelineLayout(_device, _preFilterPipelineLayout, nullptr);
    		vkDestroyPipeline(_device, toCube.pipeline, nullptr);
    		vkDestroyPipeline(_device, irradiance.pipeline, nullptr);
    		vkDestroyPipeline(_device, preFilter.pipeline, nullptr);
    		vkDestroyPipeline(_device, brdfLUT.pipeline, nullptr);
		});
}

//void VulkanEngine::convert_to_cube(){
//    /*
//    VK_CHECK(vkResetFences(_device, 1, &get_current_frame()._renderFence));
//    VkCommandBuffer cmd = get_current_frame()._mainCommandBuffer;
//    VK_CHECK(vkResetCommandBuffer(cmd, 0));
//    VkCommandBufferBeginInfo cmdInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
//    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdInfo));
//    */
//
//    very weird behavior noticed that _cubeMapTextures doesn't 
//    get overwritten after the vkCmdDispatch. Is it not being dispatched?
//
//     used _gravelImage and it was still black. -confirmed that it's not a corrupted loaded hdr.
//     This only leaves out the option that _cubeMapHDRTexture is not being updated by the compute shader.
//         is a the compute shader issue, pipelin issue, or something else?
//    
//    immediate_submit([&](VkCommandBuffer cmd) {
//        vkutil::transition_image(cmd, _loadedHDRTexture.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
//        vkutil::transition_image(cmd, _cubeMapHDRTexture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,6);
//        
//
//        //////generate the cube map from the hdr texture/////////////
//        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _rectToCubeEffect.pipeline);
//
//         bind the descriptor set containing the draw image for the compute pipeline
//        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _rectToCubePipelineLayout, 0, 1, &_rectToCubeDescriptor, 0, nullptr);
//
//        DescriptorWriter writer;
//        writer.write_image(0, _loadedHDRTexture.imageView, _defaultSamplerLinear, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
//        writer.write_image(1, _cubeMapHDRTexture.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
//        writer.update_set(_device, _rectToCubeDescriptor);
//        writer.clear();
//
//         execute the compute pipeline dispatch. We are using 16x16 workgroup size so we need to divide by it
//        uint32_t faceSize = _cubeMapHDRTexture.imageExtent.width; //the width is equal to the height
//        vkCmdDispatch(cmd, std::ceil(faceSize / 16.0), std::ceil(faceSize / 16.0), 6);
//        vkutil::transition_image(cmd, _cubeMapHDRTexture.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 6);
//
//
//    });
//
//    /*
//    VK_CHECK(vkEndCommandBuffer(cmd));
//
//    VkCommandBufferSubmitInfo cmdSubmitInfo = vkinit::command_buffer_submit_info(cmd);
//     VkSemaphoreSubmitInfo waitSemaphore = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, get_current_frame()._swapchainSemaphore);
//     VkSemaphoreSubmitInfo signalSemaphore = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, get_current_frame()._renderSemaphore);
//    VkSubmitInfo2 submit = vkinit::submit_info(&cmdSubmitInfo, nullptr, nullptr);
//    VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, get_current_frame()._renderFence));
//    */
//    
//}

void VulkanEngine::convert_to_cube() {
    /*
    VK_CHECK(vkResetFences(_device, 1, &get_current_frame()._renderFence));
    VkCommandBuffer cmd = get_current_frame()._mainCommandBuffer;
    VK_CHECK(vkResetCommandBuffer(cmd, 0));
    VkCommandBufferBeginInfo cmdInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdInfo));
    */

    immediate_submit([&](VkCommandBuffer cmd) {
        vkutil::transition_image(cmd, _loadedHDRTexture.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
        vkutil::transition_image(cmd, _cubeMapHDRTexture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 6);
        vkutil::transition_image(cmd, _irradianceMapTexture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 6);


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
        AllocatedImage preFilteredImage = create_image(cubeMapSize, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_VIEW_TYPE_CUBE, true);
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
            _preFilterDescriptor = globalDescriptorAllocator.allocate(_device, _rectToCubeDescriptorLayout);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _preFilterPipelineLayout, 0, 1, &_preFilterDescriptor, 0, nullptr);

            DescriptorWriter mipWriter;
            mipWriter.write_image(0, _cubeMapHDRTexture.imageView, _defaultSamplerLinear, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            mipWriter.write_image(1, mipView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
            mipWriter.update_set(_device, _preFilterDescriptor);
            mipWriter.clear();

            vkCmdPushConstants(cmd, _preFilterPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(float), &roughness);

            vkCmdDispatch(cmd, std::ceil(mipWidth / 16.0), std::ceil(mipHeight / 16.0), 6);
            _mainDeletionQueue.push_function([=]() {
                vkDestroyImageView(_device, mipView, nullptr);
                });

        }
        AllocatedImage oldImage = _cubeMapHDRTexture;
        _mainDeletionQueue.push_function([=]() {
            destroy_image(oldImage);
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
        /////////////////////////////////////////////////////



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
