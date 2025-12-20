find_package(Vulkan REQUIRED)

# --- NEW: Find SDL2 Automatically ---
# This looks for SDL2Config.cmake in standard paths (Mac) 
# or in the path you provide via command line (Windows)
find_package(SDL2 REQUIRED CONFIG)

add_library(vkbootstrap STATIC)
add_library(glm INTERFACE)
add_library(vma INTERFACE)
add_library(stb_image INTERFACE)
add_library(tinyobjloader STATIC)

target_sources(vkbootstrap PRIVATE 
    vkbootstrap/VkBootstrap.h
    vkbootstrap/VkBootstrap.cpp
    )

target_include_directories(vkbootstrap PUBLIC vkbootstrap)
target_link_libraries(vkbootstrap PUBLIC Vulkan::Vulkan $<$<BOOL:UNIX>:${CMAKE_DL_LIBS}>)

target_include_directories(vma INTERFACE vma)
target_include_directories(glm INTERFACE glm)

target_sources(tinyobjloader PRIVATE 
    tinyobjloader/tiny_obj_loader.h
    tinyobjloader/tiny_obj_loader.cc
    )
target_include_directories(tinyobjloader PUBLIC tinyobjloader)

# --- Removed the manual "add_library(sdl2)" block entirely ---


add_library(imgui STATIC)

target_include_directories(imgui PUBLIC imgui)

target_sources(imgui PRIVATE 
    imgui/imgui.h
    imgui/imgui.cpp
    imgui/imgui_demo.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_impl_vulkan.cpp
    imgui/imgui_impl_sdl.cpp
    )

# --- Link against the "Imported Targets" provided by find_package ---
# SDL2::SDL2 is the shared library
# SDL2::SDL2main handles the Windows entry point (WinMain)
target_link_libraries(imgui PUBLIC Vulkan::Vulkan SDL2::SDL2)

if(TARGET SDL2::SDL2main)
    target_link_libraries(imgui PUBLIC SDL2::SDL2main)
endif()

target_include_directories(stb_image INTERFACE stb_image)