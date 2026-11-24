cmake_minimum_required(VERSION 3.22.1)
project(ultrascale_graphics_layers LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_library(android-lib android)
find_library(log-lib log)
find_library(vulkan-lib vulkan)
find_library(egl-lib EGL)
find_library(gles-lib GLESv3)

add_library(ultrascale_probe SHARED graphics_probe.cpp)
target_link_libraries(ultrascale_probe PRIVATE ${android-lib} ${log-lib} ${vulkan-lib} ${egl-lib} ${gles-lib})

set(ULTRASCALE_DEFAULT_UPSCALER "$ENV{ULTRASCALE_DEFAULT_UPSCALER}" CACHE STRING "Default GLES spatial upscaler")
if(NOT ULTRASCALE_DEFAULT_UPSCALER)
    set(ULTRASCALE_DEFAULT_UPSCALER "fsr_lite")
endif()
set(ULTRASCALE_DEFAULT_CAS "$ENV{ULTRASCALE_DEFAULT_CAS}" CACHE STRING "Enable final-frame CAS by default")
if(ULTRASCALE_DEFAULT_CAS STREQUAL "")
    set(ULTRASCALE_DEFAULT_CAS "0")
endif()
set(ULTRASCALE_DEFAULT_CAS_SHARPNESS "$ENV{ULTRASCALE_DEFAULT_CAS_SHARPNESS}" CACHE STRING "Default CAS sharpness")
if(ULTRASCALE_DEFAULT_CAS_SHARPNESS STREQUAL "")
    set(ULTRASCALE_DEFAULT_CAS_SHARPNESS "0.25")
endif()

add_library(ultrascale_gles_layer SHARED GlesLayer.cpp)
set_target_properties(ultrascale_gles_layer PROPERTIES OUTPUT_NAME "GLES_ultrascale")
target_compile_definitions(ultrascale_gles_layer PRIVATE
    ULTRASCALE_DEFAULT_UPSCALER="${ULTRASCALE_DEFAULT_UPSCALER}"
    ULTRASCALE_DEFAULT_CAS=${ULTRASCALE_DEFAULT_CAS}
    ULTRASCALE_DEFAULT_CAS_SHARPNESS=${ULTRASCALE_DEFAULT_CAS_SHARPNESS})
target_link_libraries(ultrascale_gles_layer PRIVATE ${egl-lib} ${gles-lib} ${log-lib})

add_library(ultrascale_vulkan_layer SHARED VulkanLayer.cpp)
set_target_properties(ultrascale_vulkan_layer PROPERTIES OUTPUT_NAME "VkLayer_ultrascale_api_lab")
target_include_directories(ultrascale_vulkan_layer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ultrascale_vulkan_layer PRIVATE ${log-lib})
