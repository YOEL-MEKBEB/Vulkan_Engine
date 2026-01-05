// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <span>
#include <array>
#include <functional>
#include <deque>

#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vk_mem_alloc.h>

#include <fmt/core.h>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <glm/glm.hpp>


#define VK_CHECK(x)                                                       \
  do{                                                                     \
    VkResult err = x;                                                     \
    if (err) {                                                            \
            fmt::print("Detected Vulkan error: {}", string_VkResult(err)); \
            abort();                                                      \
    }                                                                     \
  } while (0)

//This struct contains the buffers and
// its allocation information as well.
// Can be used for storing sceneData,
// Vertex Buffers, Index Buffers, and
// other information as well.                                         
struct AllocatedBuffer{
  VkBuffer buffer;
  VmaAllocation allocation;
  VmaAllocationInfo info;
};


// This struct is created for the vertex
// shader. The order needs to match up with
// what is available in the shaders otherwise
// the GPU will misrepresent data or validation layers
// will be mad.
struct Vertex {
  glm::vec3 position;
	float uv_x;
	glm::vec3 normal;
	float uv_y;
	glm::vec4 color;
	glm::vec4 tangent = glm::vec4(0.f, 0.f, 0.f, 0.f);
};

// holds the resources needed for a mesh
struct GPUMeshBuffers {

    AllocatedBuffer indexBuffer;
    AllocatedBuffer vertexBuffer;
    VkDeviceAddress vertexBufferAddress;
};

// push constants for our mesh object draws
// Push constants are important here because
// it's nice to be able to just inject data into
// the shaders without having to go through the main
// pipeline. Note, it can't exceed 128 bytes, so limit it
// to like data that is shared among vertices or transformation
// matrices.
struct GPUDrawPushConstants {
    glm::mat4 worldMatrix;
    VkDeviceAddress vertexBuffer;
};

//this enum class was created with the intent of
// identifying whether an object is opaque or
// transparent.
// MainColor -> Opaque
// Transparent is self explanatory.
enum class MaterialPass :uint8_t {
    MainColor,
    Transparent,
    Other
};


// This used to store pipeline information of the
// materials from the mesh
struct MaterialPipeline {
	VkPipeline pipeline;
	VkPipelineLayout layout;
};

// A material instance contains all the information it needs
// to be a material.
// pipeline -> the pipeline information along withits layout
// materialSet -> contains the texture
// passType -> determines whether it's Opaque or transparent
struct MaterialInstance {
    MaterialPipeline* pipeline;
    VkDescriptorSet materialSet;
    MaterialPass passType;
};

//Forward declaration is required because IRenderable uses a DrawContext
// Defined in vk_engine.h
struct DrawContext;

// base class for a renderable dynamic object
class IRenderable {

    //The top matrix is the same thing as a parent matrix
    // DrawContext contains all of the Surfaces of the scene.
    virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) = 0;
};

// implementation of a drawable scene node.
// the scene node can hold children and will also keep a transform to propagate
// to them
struct Node : public IRenderable {

    // parent pointer must be a weak pointer to avoid circular dependencies
    // remember that weak pointers don't claim ownership so it won't prevent
    // the parent from being deleted.
    std::weak_ptr<Node> parent;

    // Even though each child has only one parent remember that
    // unique pointers don't allow for the use of weak pointers
    // because they don't have a control block. So it's kind of a no
    // choice situation of using shared pointers because the child
    // needs a pointer to it's parent.
    std::vector<std::shared_ptr<Node>> children;

    glm::mat4 localTransform;
    glm::mat4 worldTransform;

    int useNormal = 0;

    //recursive propagation of transforming an object and it's children
    void refreshTransform(const glm::mat4& parentMatrix)
    {
        worldTransform = parentMatrix * localTransform;
        for (auto c : children) {
            c->refreshTransform(worldTransform);
        }
    }

    //This draws everything in the node including the children
    virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx)
    {
        // draw children
        for (auto& c : children) {
            c->Draw(topMatrix, ctx);
        }
    }
};
