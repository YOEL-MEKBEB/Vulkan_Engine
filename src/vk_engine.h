// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include "vulkan/vulkan_core.h"
#include <cstdint>
#include <vk_types.h>

class VulkanEngine {
public:

	bool _isInitialized{ false };
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

VkSwapchainKHR _swapchain;
VkFormat _swapchainImageFormat;

std::vector<VkImage> _swapchainImages;
std::vector<VkImageView> _swapchainImageViews;
VkExtent2D _swapchainExtent;


private:
	void init_vulkan();
	void init_swapchain();
	void init_commands();
	void init_sync_structures();
	void create_swapchain(uint32_t width, uint32_t height);
	void destroy_swapchain();
	
};
