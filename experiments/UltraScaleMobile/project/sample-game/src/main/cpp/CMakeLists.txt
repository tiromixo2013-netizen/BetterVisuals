cmake_minimum_required(VERSION 3.22.1)
project(ultrascale_sample_game LANGUAGES CXX)

set(ULTRASCALE_BACKEND_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../unity-backend")
add_library(sample_vulkan_probe SHARED
    vulkan_probe.cpp
    native_upscale_jni.cpp
    "${ULTRASCALE_BACKEND_ROOT}/native/ultrascale_gles_backend.cpp"
)
target_include_directories(sample_vulkan_probe PRIVATE
    "${ULTRASCALE_BACKEND_ROOT}/include"
)
target_compile_features(sample_vulkan_probe PRIVATE cxx_std_17)

find_library(vulkan-lib vulkan)
find_library(egl-lib EGL)
find_library(gles-lib GLESv3)
find_library(log-lib log)
target_link_libraries(sample_vulkan_probe PRIVATE
    ${vulkan-lib}
    ${egl-lib}
    ${gles-lib}
    ${log-lib}
)
