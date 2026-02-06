
# Add source to this project's executable.
add_executable(engine
    main.cpp
    vk_engine.cpp
    vk_engine.h
    vk_types.h
    vk_initializers.cpp
    vk_initializers.h
    vk_images.cpp
    vk_images.h
    vk_pipelines.cpp
    vk_pipelines.h
    vk_descriptors.cpp
    vk_descriptors.h
    vk_loader.cpp
    vk_loader.h
    camera.cpp
    camera.h
    shadowPipeline.cpp
    shadowPipeline.h
    )


# set_property(TARGET vulkan_guide PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:vulkan_guide>")

# target_include_directories(vulkan_guide PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# target_link_libraries(vulkan_guide vkbootstrap vma glm tinyobjloader imgui stb_image)

# target_link_libraries(vulkan_guide Vulkan::Vulkan SDL2::SDL2 SDL2::SDL2main fmt::fmt)

# add_dependencies(vulkan_guide Shaders)



set_property(TARGET engine PROPERTY CXX_STANDARD 20)
target_compile_definitions(engine PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE)
target_include_directories(engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(engine PUBLIC vma glm Vulkan::Vulkan fmt::fmt stb_image SDL2::SDL2 vkbootstrap imgui fastgltf::fastgltf)

target_precompile_headers(engine PUBLIC <optional> <vector> <memory> <string> <vector> <unordered_map> <glm/mat4x4.hpp>  <glm/vec4.hpp> <vulkan/vulkan.h>)

add_dependencies(engine Shaders)
if(WIN32)
  add_custom_command(TARGET engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:engine> $<TARGET_FILE_DIR:engine>
    COMMAND_EXPAND_LISTS
    )
endif()
