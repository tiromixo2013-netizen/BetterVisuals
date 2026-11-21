cmake_minimum_required(VERSION 3.22.1)
project(ultrascale_graphics_layers LANGUAGES C CXX)

# ShadowHook is used only after this layer is loaded by Android's documented
# graphics-layer loader in an opt-in/debuggable test target.
find_package(shadowhook REQUIRED CONFIG)
find_package(bytehook REQUIRED CONFIG)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_library(android-lib android)
find_library(log-lib log)
find_library(vulkan-lib vulkan)
find_library(egl-lib EGL)
find_library(gles-lib GLESv3)

add_library(ultrascale_probe SHARED graphics_probe.cpp)
target_link_libraries(ultrascale_probe PRIVATE ${android-lib} ${log-lib} ${vulkan-lib} ${egl-lib} ${gles-lib})

add_library(ultrascale_gles_layer SHARED GlesLayer.cpp)
set_target_properties(ultrascale_gles_layer PROPERTIES OUTPUT_NAME "GLES_ultrascale")
target_link_libraries(ultrascale_gles_layer PRIVATE ${egl-lib} ${gles-lib} ${log-lib} shadowhook::shadowhook bytehook::bytehook)

add_library(ultrascale_vulkan_layer SHARED VulkanLayer.cpp)
set_target_properties(ultrascale_vulkan_layer PROPERTIES OUTPUT_NAME "VkLayer_ultrascale_api_lab")
target_include_directories(ultrascale_vulkan_layer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ultrascale_vulkan_layer PRIVATE ${log-lib})
