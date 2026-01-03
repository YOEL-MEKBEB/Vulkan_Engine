#pragma once
#include <vector>
#include "vk_types.h"
//This file handles everything related to Descriptor sets in vulkan which are the way
// to connect data between CPU and GPU 

//The descriptorLayoutBuilder lets you create descriptor set layouts really easily
struct DescriptorLayoutBuilder {

    std::vector<VkDescriptorSetLayoutBinding> bindings;

    //this creates a DescriptorSetLayoutBinding and adds it into binding
    void add_binding(uint32_t binding, VkDescriptorType type);

    //clears the bindings vector
    void clear();

    //the descriptor set layout gets created here and determines whether it's a vertex or fragment shader
    VkDescriptorSetLayout build(VkDevice device, VkShaderStageFlags shaderStages, void* pNext = nullptr, VkDescriptorSetLayoutCreateFlags flags = 0);
};

// this sets up a system for allocating memory for the resources like
// textures and buffers for the GPU to sample efficiently. It lets you allocate
// memory initially to let you populate it as you need. It has a major problem
// where it once allocated the size doesn't and this will be fixed by DescriptorAllocatorGrowable
// below.
struct DescriptorAllocator {
  struct PoolSizeRatio{
		VkDescriptorType type;
		float ratio;
    };

    VkDescriptorPool pool;

    void init_pool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios);
    void clear_descriptors(VkDevice device);
    void destroy_pool(VkDevice device);

    VkDescriptorSet allocate(VkDevice device, VkDescriptorSetLayout layout);
};

//This does the same thing as DescriptorAllocator but is also capable of resizing
// itself. The resize is 1.5 times.
struct DescriptorAllocatorGrowable{
  public:
  	struct PoolSizeRatio {
  		VkDescriptorType type;
  		float ratio;
  	};

    void init(VkDevice device, uint32_t initialSets, std::span<PoolSizeRatio> poolRatios);
    void clear_pools(VkDevice device);
    void destroy_pools(VkDevice device);

    VkDescriptorSet allocate(VkDevice device, VkDescriptorSetLayout layout, void* pNext = nullptr);
  private:
  	VkDescriptorPool get_pool(VkDevice device);
  	VkDescriptorPool create_pool(VkDevice device, uint32_t setCount, std::span<PoolSizeRatio> poolRatios);

  	std::vector<PoolSizeRatio> ratios;
  	std::vector<VkDescriptorPool> fullPools;
  	std::vector<VkDescriptorPool> readyPools;
  	uint32_t setsPerPool;
 };

//Once the memory has been allocated you need some way to populate it with the information
// you need. This struct is the answer to that it lets you bind images(textures) and buffers
// to the descriptor sets. 
struct DescriptorWriter{
  // queues are used here because when they get resized they don't
  // allocate new memory block and copy data over. The data inside
  // them is not contiguous. This ensures that any pointers
  // towards any data inside them will not be invalidated by a resize.
  std::deque<VkDescriptorImageInfo> imageInfos;
  std::deque<VkDescriptorBufferInfo> bufferInfos;

  //a vector is usable here because there will be no pointer pointing
  // to the data in writes. writes is a vector that contains all of the
  // VkWriteDescriptorSets created by ImageInfos and bufferInfos. Each VkWriteDescriptorSet
  // contains a pointer towards one of the imageInfos or bufferInfos, so it's very
  // important that imageInfos and bufferInfos are queues.
  std::vector<VkWriteDescriptorSet> writes;

  //this function populates imageInfos and writes and determines which binding in the set
  // this data will have. dstSet is left empty here and update_set must be called after setting up the
  // DescriptorSet.
  void write_image(int binding,VkImageView image,VkSampler sampler , VkImageLayout layout, VkDescriptorType type);

  //this function populates bufferInfos and writes and determines which binding in the set
  // the data will have.dstSet is left empty here and update_set must be called after setting up the
  // DescriptorSet.
  void write_buffer(int binding,VkBuffer buffer,size_t size, size_t offset,VkDescriptorType type); 

  void clear();

  //sets dstSet of each VkWriteDescriptorSet with the provided DescriptorSet.
  // So in summary you determine what the binding is in write_image and write_buffer
  // and you determine which set here in update_set.
  void update_set(VkDevice device, VkDescriptorSet set);
};
