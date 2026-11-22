cmake_minimum_required(VERSION 3.22.1)
project(ultrascale_unity_bridge LANGUAGES CXX)

option(ULTRASCALE_BUILD_TESTS "Build host/native contract tests" OFF)

add_library(ultrascale_unity_bridge SHARED
    ultrascale_unity_bridge.cpp
    ultrascale_gles_backend.cpp
)
target_compile_features(ultrascale_unity_bridge PRIVATE cxx_std_17)
target_compile_options(ultrascale_unity_bridge PRIVATE -Wall -Wextra -Werror)
set_target_properties(ultrascale_unity_bridge PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

if(ANDROID)
    find_library(GLESV3_LIBRARY GLESv3 REQUIRED)
    find_library(EGL_LIBRARY EGL REQUIRED)
    target_link_libraries(ultrascale_unity_bridge PRIVATE ${GLESV3_LIBRARY} ${EGL_LIBRARY})
endif()

if(ULTRASCALE_BUILD_TESTS)
    enable_testing()
    add_executable(ultrascale_unity_bridge_tests
        tests/ultrascale_unity_bridge_tests.cpp
        ultrascale_unity_bridge.cpp
        ultrascale_gles_backend.cpp
    )
    target_compile_features(ultrascale_unity_bridge_tests PRIVATE cxx_std_17)
    target_compile_options(ultrascale_unity_bridge_tests PRIVATE -Wall -Wextra -Werror)
    add_test(NAME ultrascale_unity_bridge_contract
             COMMAND ultrascale_unity_bridge_tests)
endif()
