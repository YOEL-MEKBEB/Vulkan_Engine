// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include "vulkan/vulkan_core.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vk_types.h>
#include "vk_descriptors.h"
#include <glm/glm.hpp>
#include <vk_loader.h>
#include <camera.h>

struct EngineStats{
	float frametime;
	int triangle_count;
	int drawcall_count;
	float scene_update_time;
	float mesh_draw_time;
};

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

struct ComputePushConstantOriginal{
	float inColorX;
	float inColorY;
};

struct ComputePushConstant{
	glm::vec4 data1;
	glm::vec4 data2;
	glm::vec4 data3;
	glm::vec4 data4;
};

struct ComputeEffect{
	const char* name;

	VkPipeline pipeline;
	VkPipelineLayout layout;

	ComputePushConstant data;
};

struct AllocatedImage {
    VkImage image;
    VkImageView imageView;
    VmaAllocation allocation;
    VkExtent3D imageExtent;
    VkFormat imageFormat;
};

struct GPUSceneData {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 viewproj;
    glm::vec4 ambientColor;
    glm::vec4 specularColor;
    glm::vec4 sunlightDirection; // w for sun power
    glm::vec4 sunlightColor;
    glm::vec4 cameraPosition;
    int shininess;
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

	//will be used for dynamic descriptor allocation on runtime
	DescriptorAllocatorGrowable _frameDescriptors;
	
};


struct GLTFMetallic_Roughness {
	MaterialPipeline opaquePipeline;
	MaterialPipeline transparentPipeline;

	VkDescriptorSetLayout materialLayout;

	struct MaterialConstants {
		glm::vec4 colorFactors;
		glm::vec4 metal_rough_factors;
		//padding, we need it anyway for uniform buffers
		glm::vec4 extra[14];
	};

	struct MaterialResources {
		AllocatedImage colorImage;
		VkSampler colorSampler;
		AllocatedImage metalRoughImage;
		VkSampler metalRoughSampler;
		VkBuffer dataBuffer;
		uint32_t dataBufferOffset;
	};

	DescriptorWriter writer;

	void build_pipelines(VulkanEngine* engine);
	void clear_resources(VkDevice device);

	MaterialInstance write_material(VkDevice device, MaterialPass pass, const MaterialResources& resources, DescriptorAllocatorGrowable& descriptorAllocator);
};


struct MeshNode : public Node{
	std::shared_ptr<MeshAsset> mesh;

	virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) override;
};


struct RenderObject {
    uint32_t indexCount;
    uint32_t firstIndex;
    VkBuffer indexBuffer;
    
    MaterialInstance* material;
		Bounds bounds;
    glm::mat4 transform;
    VkDeviceAddress vertexBufferAddress;
};


struct DrawContext{
	std::vector<RenderObject> OpaqueSurfaces;
	std::vector<RenderObject> TransparentSurfaces;
};

constexpr unsigned int FRAME_OVERLAP = 2;

class VulkanEngine {
public:

	Camera mainCamera;
	EngineStats stats;
	
	bool _isInitialized{ false };
	bool _resize_requested{ false };

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
	AllocatedImage _depthImage;
	VkExtent2D _drawExtent;
	float renderScale = 1.f;

	DescriptorAllocatorGrowable globalDescriptorAllocator;

	VkDescriptorSet _drawImageDescriptors;
	VkDescriptorSetLayout _drawImageDescriptorLayout;

	VkPipeline _gradientPipeline;
	VkPipelineLayout _gradientPipelineLayout;
	ComputePushConstantOriginal _breathColorPushConst;

	std::vector<ComputeEffect> backgroundEffects;
	int currentBackgroundEffect{0};

	//immediate submit structure
	VkFence _imFence;
	VkCommandBuffer _imCommandBuffer;
	VkCommandPool _imCommandPool;

	void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);

	//triangle pipelines finally. ;)
	VkPipelineLayout _trianglePipelineLayout;
	VkPipeline _trianglePipeline;

	void init_triangle_pipeline();

	VkPipelineLayout _meshPipelineLayout;
	VkPipeline _meshPipeline;

	GPUMeshBuffers rectangle;
	std::vector<std::shared_ptr<MeshAsset>> testMeshes;
	

	GPUMeshBuffers upload_mesh(std::span<uint32_t> indices, std::span<Vertex> vertices);
	AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
	void destroy_buffer(const AllocatedBuffer& buffer);

	//contains all of the view and projections matrices
	// and data for lighting models
	GPUSceneData sceneData;
	VkDescriptorSetLayout _gpuSceneDataDescriptorLayout;

	AllocatedImage create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
	AllocatedImage create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
	void destroy_image(const AllocatedImage& img);

	// These are default textures for when loading textures fail.
	AllocatedImage _whiteImage;
	AllocatedImage _blackImage;
	AllocatedImage _greyImage;
	AllocatedImage _errorCheckerboardImage;

  VkSampler _defaultSamplerLinear;
	VkSampler _defaultSamplerNearest;

	//descriptor set layout for textures
	VkDescriptorSetLayout _singleImageDescriptorLayout;

	//materials
	MaterialInstance defaultData;
	GLTFMetallic_Roughness metalRoughMaterial;

	DrawContext mainDrawContext;
	std::unordered_map<std::string, std::shared_ptr<Node>> loadedNodes;

	std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>> loadedScenes;


	void update_scene();



private:
	void init_vulkan();
	void init_swapchain();
	void init_commands();
	void init_sync_structures();
	void create_swapchain(uint32_t width, uint32_t height);
	void destroy_swapchain();
	void draw_background(VkCommandBuffer cmd);
	void draw_geometry(VkCommandBuffer cmd);
	void init_descriptors();
	void init_pipelines();
	void init_background_pipelines();
	void init_imgui();
	void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView);
	void init_mesh_pipeline();
	void init_default_data();
	void resize_swapchain();
};
