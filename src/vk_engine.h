// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include "vulkan/vulkan_core.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>
#include <vk_types.h>
#include "vk_descriptors.h"
#include <glm/glm.hpp>
#include <vk_loader.h>
#include <camera.h>

// All of the stats for debugging and displaying
// on the screen using ImGui
struct EngineStats{
	float frametime;
	int triangle_count;
	int drawcall_count;
	float scene_update_time;
	float mesh_draw_time;
};

//This.... This is amazing. All you have to do
// is put in a lambda function that contains
// the deleter of the data structure.
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

// struct ComputePushConstantOriginal{
// 	float inColorX;
// 	float inColorY;
// };

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
	// until the command buffer is submitted to the GPU through a vkQueueSubmit call.
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


//A struct that contains all of the properties 
// in a GLTF file. At the moment, the metal
// and rough factors are not being used and will be
// used once PBR is implemented.
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
		AllocatedImage normalImage;
		VkSampler normalSampler;
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

//The object to be rendered.
// Contains the index Buffer, the material associated
// with it. The bounds for frustum culling
//
// The transform and vertexBufferAddress are dynamic data
// that will passed as push constants to the vertex shader.
struct RenderObject {
    uint32_t indexCount;
    uint32_t firstIndex;
    VkBuffer indexBuffer;
    
    MaterialInstance* material;
		Bounds bounds;
    glm::mat4 transform;
    VkDeviceAddress vertexBufferAddress;

    int useNormal = 0; //only do normal mapping for objects that have a normal texture.
};




//Contains all of the renderObjects in the scene
// It divides them into Opaque and transparent
// surfaces. The division is important because
// the Opaque surfaces will have different depth testing
// methods than the transparent surfaces. This means that
// they will pass through different pipelines since depth
// testing is hardcoded into the pipeline.
struct DrawContext{
	std::vector<RenderObject> OpaqueSurfaces;
	std::vector<RenderObject> TransparentSurfaces;
};

//variable used for determining whether the
// rendering is single/double/triple buffered.
constexpr unsigned int FRAME_OVERLAP = 2;

//The main class, where everything happens.
class VulkanEngine {
public:

	// main camera for rendering the scene on to
	// the screen
	Camera mainCamera;

	// camera for rendering a shadow map texture
	// to create shadows in the final scene.
	Camera lightCamera;

	
	EngineStats stats;
	
	bool _isInitialized{ false };
	bool _resize_requested{ false };

	//number of frames that have been rendered
	int _frameNumber {0};
	
	bool stop_rendering{ false };

	/////////////// Window management varaibles/////
	VkExtent2D _windowExtent{ 1700 , 900 };

	struct SDL_Window* _window{ nullptr };

	static VulkanEngine& Get();
	///////////////////////////////////////////////

	///////////////main functions/////////////
	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

	//draw loop
	void draw();

	//run main loop
	void run();
	/////////////////////////////////////////


	/////////////////////// Vulkan initialization variables/////////////
	VkInstance _instance; // The connection to the Vulkan library/loader
	VkDebugUtilsMessengerEXT _debug_messenger; // Handle for capturing debug/validation messages
	VkPhysicalDevice _chosenGPU; // Handle to the actual physical hardware (GPU)
	VkDevice _device; // The logical interface used to command the GPU
	VkSurfaceKHR _surface; // The abstraction of the OS window to render into
	///////////////////////////////////////////////////////////////////

	
	//////////////////swapchain variables////////////////
	VkSwapchainKHR _swapchain;
	VkFormat _swapchainImageFormat;

	//images contained in the swap chain
	std::vector<VkImage> _swapchainImages;
	std::vector<VkImageView> _swapchainImageViews;
	VkExtent2D _swapchainExtent;
	///////////////////////////////////////////////////////////////////
	
	///////////////////Frames Data variables///////
	//data held by each frame and the queue to submit to.
	FrameData _frames[FRAME_OVERLAP];
	FrameData& get_current_frame(){ return _frames[_frameNumber % FRAME_OVERLAP]; }
	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;
	//////////////////////////////////////////////


	//data structure for deleting stuff;
	DeletionQueue _mainDeletionQueue;

	//////////////////////Image Allocation variables////////////////
	//data structure for allocating custom made image.
	VmaAllocator _allocator;
	AllocatedImage _drawImage; //main image
	AllocatedImage _depthImage; //depth testing image
	AllocatedImage _lightDepthImage; //shadow map image
	VkExtent2D _drawExtent;
	////////////////////////////////////////////////////////

	
	float renderScale = 1.f;


	/////////////////////Descriptor Set variables/////////////
	DescriptorAllocatorGrowable globalDescriptorAllocator;

	VkDescriptorSet _drawImageDescriptors;
	VkDescriptorSetLayout _drawImageDescriptorLayout;
	////////////////////////////////////////////////////////
	

	////////////////////Compute Shader////////////////
	//// This is for the gradient background in the compute shader
	VkPipeline _gradientPipeline;
	VkPipelineLayout _gradientPipelineLayout;
	// ComputePushConstantOriginal _breathColorPushConst;
	//The vector allows for swappable backgrounds in the
	// compute shader
	std::vector<ComputeEffect> backgroundEffects;
	int currentBackgroundEffect{0};
	////////////////////////////////////////////////


	////////////////Immediate submit////////////////
	//immediate submit structures.
	VkFence _imFence;
	VkCommandBuffer _imCommandBuffer;
	VkCommandPool _imCommandPool;
	// Immediate submit is used to force synchronous work in an asynchronous system
	// For example, sending a vertex/index buffer or an image buffer to the GPU.
	// We don't want to be rendering stuff that has not been properly staged yet.
	void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);
	/////////////////////////////////////////////////

	//triangle pipelines finally. ;)////////////////////////
	VkPipelineLayout _trianglePipelineLayout;
	VkPipeline _trianglePipeline;
	void init_triangle_pipeline();
	///////////////////////////////////////////////
	
	////////////Mesh pipeline and scene Data//////////////////
	VkPipelineLayout _meshPipelineLayout;
	VkPipeline _meshPipeline;
	//contains all of the view and projections matrices
	// and data for lighting models
	GPUSceneData sceneData;
	VkDescriptorSetLayout _gpuSceneDataDescriptorLayout;
	///////////////////////////////////////////////

	/////////////Shadow map pipeline and scene Data/////////////////
	VkPipelineLayout _shadowPipelineLayout;
	VkPipeline _shadowPipeline;
	GPUSceneData shadowSceneData;
	VkDescriptorSetLayout _shadowSceneDataDescriptorLayout;
	//////////////////////////////////////////////
	
	//////////////// Buffers /////////////////////
	GPUMeshBuffers rectangle;
	std::vector<std::shared_ptr<MeshAsset>> testMeshes;
	GPUMeshBuffers upload_mesh(std::span<uint32_t> indices, std::span<Vertex> vertices);
	//Functions to Create and destroy the buffers.
	AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
	void destroy_buffer(const AllocatedBuffer& buffer);
	GPUMeshBuffers cube;
	////////////////////////////////////////////////

	//////////////// Images /////////////////////////
	AllocatedImage create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, VkImageViewType viewType, bool mipmapped = false);
	AllocatedImage create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, VkImageViewType viewType, bool mipmapped = false);
	void destroy_image(const AllocatedImage& img);

	// These are default textures for when loading textures fail.
	AllocatedImage _whiteImage;
	AllocatedImage _blackImage;
	AllocatedImage _greyImage;
	AllocatedImage _errorCheckerboardImage;

  VkSampler _defaultSamplerLinear;  // linear interpolation sampling
	VkSampler _defaultSamplerNearest; // nearest neighbors sampling
	VkSampler _defaultCubeSampler;
	
	//descriptor set layout for textures
	VkDescriptorSetLayout _singleImageDescriptorLayout;
	//////////////////////////////////////////////

	/////////////////////materials///////////////
	MaterialInstance defaultData;
	GLTFMetallic_Roughness metalRoughMaterial;
	/////////////////////////////////////////////

	DrawContext mainDrawContext;  // final draw context
	DrawContext lightDrawContext; // draw context for shadow mapping
	
	////////////loaded meshes and scenes from GLTF files/////////////
	std::unordered_map<std::string, std::shared_ptr<Node>> loadedNodes;
	std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>> loadedScenes;
	void update_scene();
	////////////////////////////////////////////////////////////////

	///////////////Default loaded textures///////////
  AllocatedImage _gravelImage;
  AllocatedImage _gravelRoughness;
	AllocatedImage _gravelNormal;
	//////////////////////////////////////////////

	// std::vector<AllocatedImage> _cubeMapTextures;
	AllocatedImage _cubeMapTextures;
	AllocatedImage _cubeMapHDRTexture;
	AllocatedImage _loadedHDRTexture;
	AllocatedImage _irradianceMapTexture;
	VkDescriptorSetLayout _rectToCubeDescriptorLayout;
	VkPipelineLayout _rectToCubePipelineLayout;
	ComputeEffect _rectToCubeEffect;
	VkDescriptorSet _rectToCubeDescriptor;
	
	/////////////cubMap pipeline and scene Data/////////////
	VkPipelineLayout _skyboxPipelineLayout;
	VkPipeline _skyboxPipeline;
	//////////////////////////////////////////////

	//defualt PBR rendering datapoints///////////
	glm::vec4 colorFactors = glm::vec4{1,1,1,1};
	glm::vec4 metal_rough_factors = glm::vec4{0, 1, 0, 0};
	AllocatedBuffer materialConstants;
	//////////////////////////////////
	  

private:
	void init_vulkan(); //initialize the vulkan instance
	void init_swapchain(); //initialize the swapchain variables
	void init_commands(); // initialize the command buffers
	void init_sync_structures(); // initialize the fences and semaphores
	void create_swapchain(uint32_t width, uint32_t height); // create swapchain images
	void resize_swapchain(); 
	void destroy_swapchain();
	void draw_background(VkCommandBuffer cmd); //draws the background using a compute shader
	void draw_geometry(VkCommandBuffer cmd); //draws loadedNodes and loadedScenes
	void init_descriptors(); //initialize the descriptor sets
	void init_pipelines(); //calls all the pipeline initialization function
	void init_background_pipelines(); //initialize the compute shader pipeline
	void init_shadow_map_pipeline(); // initializes the shadow maps rendering pipeline
	void init_skybox_pipeline();
	void init_mesh_pipeline(); //initialize the mesh pipelines
	void init_imgui(); 
	void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView);
	void init_default_data(); //set the default data for textures and transformations
	void render_shadow_map(VkCommandBuffer cmd);
	void render_skybox(VkCommandBuffer cmd, VkRenderingInfo& renderInfo);
	void load_cube_map();
	void init_rect_to_cube_pipeline();
	void convert_to_cube();
};
