#pragma once
#include "vk_descriptors.h"
#include <vk_types.h>
#include <unordered_map>
#include <filesystem>


//This bound struct is used for frustum culling
// which is detailed in vk_engine.cpp
// The sphereRadius is not used at the moment
// and will be used in the future with more
// advanced culling methods.
struct Bounds{
    glm::vec3 origin;
    float sphereRadius;
    glm::vec3 extents;  
};

// This is literally just a struct
// containing a struct but this is important for
// the sake of scalability of the code base.
// If a feature needs to be added to GLTFMaterial,
// There will be no need to refactor the function
// parameters.
struct GLTFMaterial {
	MaterialInstance data;
};


// A given mesh asset will have a name, loaded from the file,
// and then the mesh buffers. But it will also have an array
// of GeoSurface that has the sub-meshes of this specific mesh.
//  When rendering, each submesh will be its own draw. We will
// use StartIndex and count for that drawcall as we will be
// appending all the vertex data of each surface into the same buffer.
struct GeoSurface {
    uint32_t startIndex;
    uint32_t count;
    Bounds bounds;
    std::shared_ptr<GLTFMaterial> material;
};


// The meshAsset basically means that this is one object
// with all of these surfaces that contain submeshes and materials.
// The string is used for identification since GLTF files have names
// inside them. We will also use it for creating an unordered_map for
// fast lookups when rendering.
struct MeshAsset {
    std::string name;

    std::vector<GeoSurface> surfaces;
    GPUMeshBuffers meshBuffers;
};

//forward declaration
class DescriptorAllocatorGrowable;
struct AllocatedImage;

class VulkanEngine;

// std::optional basically means that the outpu of this function
// could be invalid so it won't be a good idea to assume that
// this always works. In the main code you must check with has_value().
// 
// This function load an Mesh  from the GLTF file
// but it will only load in the vertices and the normals. Nothing else
// It's usefull for testing purposes but for a complete function
// check loadGltf at the bottom.
std::optional<std::vector<std::shared_ptr<MeshAsset>>> loadGltfMeshes(VulkanEngine* engine, std::filesystem::path filePath);



// LoadedGLTF is a struct that contains all the information of about the
// GLTF file and stores it for fast lookups using unordered_map. If you
// check the vk_loader.cpp file tou will see that the data points are
// actually stored contiguously using vectors and only the pointers get
// transferred to the maps.
struct LoadedGLTF : public IRenderable{
public:
    // storage for all the data on a given glTF file
    std::unordered_map<std::string, std::shared_ptr<MeshAsset>> meshes;
    std::unordered_map<std::string, std::shared_ptr<Node>> nodes;

    // These 2 data points contain almost exact same information
    // I added the vector because images in GLTF files can be unnamed. If everything
    // in the files is unnamed then the unordered_map will only have access to
    // one of them while everything gets allocated. When the struct gets deleted
    // it won't be able to delete the other unnamed images, causing a memory leak.
    std::unordered_map<std::string, AllocatedImage> images;

    //So during the deletion process the vector is what's used for deleting.
    // The vector won't be used for lookup purposes during the rendering, it's
    // only for deletion purposes.
    std::vector<AllocatedImage> imageVector;
    
    std::unordered_map<std::string, std::shared_ptr<GLTFMaterial>> materials;

    // nodes that dont have a parent, for iterating through the file in tree order
    std::vector<std::shared_ptr<Node>> topNodes;

    std::vector<VkSampler> samplers;

    DescriptorAllocatorGrowable descriptorPool;

    AllocatedBuffer materialDataBuffer;

    VulkanEngine* creator;
    int useNormal;
    int useMetalTex;
    int useAOTex;
    
    ~LoadedGLTF();

    virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx);

private:
    void clearAll();
};

// this is the complete version loadGltfMeshes because it loads the images, vertices,
// normals, vertex colors, nodes, meshes. Basically everything. It stores it all in
// an instance of the LoadedGLTF struct. 
std::optional<std::shared_ptr<LoadedGLTF>> loadGltf(VulkanEngine* engine, std::filesystem::path filePath);
