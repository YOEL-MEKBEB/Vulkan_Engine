// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include "vulkan/vulkan_core.h"
#include <cstdint>
#include <functional>
#include <vk_types.h>
#include "vk_descriptors.h"

struct DeletionQueue{
	std::deque<std::function<void()>> deleters;

	void push_function(std::function<void()>&& function){
		deleters.push_back(function);
	}

	void flush(){
		for(auto it = deleters.rbegin(); it != deleters.rend(); it++){
			(*it)(); //call functions
		}
		deleters.clear();
	}

};


struct AllocatedImage {
    VkImage image;
    VkImageView imageView;
    VmaAllocation allocation;
    VkExtent3D imageExtent;
    VkFormat imageFormat;
};

struct FrameData{
	//A VkCommandPool is created from the VkDevice, and you
	// need the index of the queue family this command pool will create commands from.
	//Think of the VkCommandPool as the allocator for a VkCommandBuffer.
	VkCommandPool _commandPool;


	//All commands for GPU get recorded in a VkCommandBuffer.
	// All of the functions that will execute GPU work won’t do anything
	// until the command buffer is submitted to the GPU through a VkQueueSubmit call.
	VkCommandBuffer _mainCommandBuffer;

	//VkSemaphores handle resource management purely in the GPU. They make sure
	// that all the tasks are done in a designeated order
	VkSemaphore _swapchainSemaphore, _renderSemaphore;

	//VkFence handles resource management between the CPU and GPU.
	// The CPU always closes the fence while the GPU always opens it.
	// Example. CPU writes to the command buffer and sends it to the GPU.
	// It then closes a fence to that. The GPU processes the command buffer
	// and opens the fence. While the fence is closed the CPU can't overwrite
	// the command buffer in the GPU to prevent the command buffer from being
	// overwritten before it's fully processed
	VkFence _renderFence;
	DeletionQueue _deletionQueue;
	
};

constexpr unsigned int FRAME_OVERLAP = 2;

class VulkanEngine {
public:

	bool _isInitialized{ false };

	//number of frames that have been rendered
	int _frameNumber {0};
	
	bool stop_rendering{ false };

	VkExtent2D _windowExtent{ 1700 , 900 };

	struct SDL_Window* _window{ nullptr };

	static VulkanEngine& Get();

	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

	//draw loop
	void draw();

	//run main loop
	void run();


	VkInstance _instance; // The connection to the Vulkan library/loader
	VkDebugUtilsMessengerEXT _debug_messenger; // Handle for capturing debug/validation messages
	VkPhysicalDevice _chosenGPU; // Handle to the actual physical hardware (GPU)
	VkDevice _device; // The logical interface used to command the GPU
	VkSurfaceKHR _surface; // The abstraction of the OS window to render into

	//swapchain variables
	VkSwapchainKHR _swapchain;
	VkFormat _swapchainImageFormat;

	//images contained in the swap chain
	std::vector<VkImage> _swapchainImages;
	std::vector<VkImageView> _swapchainImageViews;
	VkExtent2D _swapchainExtent;

	//data held by each frame and the queue to submit to.
	FrameData _frames[FRAME_OVERLAP];
	FrameData& get_current_frame(){ return _frames[_frameNumber % FRAME_OVERLAP]; }
	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	//data structure for deleting stuff;
	DeletionQueue _mainDeletionQueue;

	//data structure for allocating custom made image.
	VmaAllocator _allocator;
	AllocatedImage _drawImage;
	VkExtent2D _drawExtent;

	DescriptorAllocator globalDescriptorAllocator;

	VkDescriptorSet _drawImageDescriptors;
	VkDescriptorSetLayout _drawImageDescriptorLayout;

	VkPipeline _gradientPipeline;
	VkPipelineLayout _gradientPipelineLayout;

private:
	void init_vulkan();
	void init_swapchain();
	void init_commands();
	void init_sync_structures();
	void create_swapchain(uint32_t width, uint32_t height);
	void destroy_swapchain();
	void draw_background(VkCommandBuffer cmd);
	void init_descriptors();
	void init_pipelines();
	void init_background_pipelines();
};

