# Integration Tests CMakeLists.txt

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/mvgal
)

link_directories(
    ${CMAKE_BINARY_DIR}
)

# Multi-GPU validation test
add_executable(test_multi_gpu
    test_multi_gpu_validation.c
)

target_link_libraries(test_multi_gpu
    mvgal_core
    Threads::Threads
    ${DRM_IMPORTED_TARGET}
    ${PCI_IMPORTED_TARGET}
    ${UDEV_IMPORTED_TARGET}
    m
)

add_test(NAME test_multi_gpu COMMAND test_multi_gpu)

if(WITH_VULKAN AND Vulkan_FOUND)
    add_executable(test_vulkan_layer_submit
        test_vulkan_layer_submit.c
    )

    target_link_libraries(test_vulkan_layer_submit
        PRIVATE
            ${Vulkan_LIBRARIES}
    )

    target_include_directories(test_vulkan_layer_submit
        PRIVATE
            ${Vulkan_INCLUDE_DIRS}
    )

    target_compile_definitions(test_vulkan_layer_submit
        PRIVATE
            MVGAL_TEST_LAYER_DIR="$<TARGET_FILE_DIR:mvgal_vulkan_layer>"
            MVGAL_TEST_VK_ICD_PATH="/usr/share/vulkan/icd.d/lvp_icd.x86_64.json"
    )

    add_test(NAME test_vulkan_layer_submit COMMAND test_vulkan_layer_submit)
endif()

# OpenCL interception test
if(MVGAL_BUILD_API AND OpenCL_FOUND)
    add_executable(test_opencl_intercept
        test_opencl_intercept.c
    )

    target_link_libraries(test_opencl_intercept
        PRIVATE
            mvgal_core
            Threads::Threads
            ${OpenCL_LIBRARIES}
            dl
            m
    )

    target_include_directories(test_opencl_intercept
        PRIVATE
            ${OpenCL_INCLUDE_DIRS}
    )

    add_test(NAME test_opencl_intercept COMMAND test_opencl_intercept)
endif()

# D3D11/D3D12 wrapper interception tests
if(MVGAL_BUILD_API)
    add_executable(test_d3d_wrapper
        test_d3d_wrapper.c
    )

    target_link_libraries(test_d3d_wrapper
        PRIVATE
            mvgal_d3d
            dl
            m
    )

    # Suppress unused variable warnings in test file (mock variables for documentation)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
        target_compile_options(test_d3d_wrapper PRIVATE -Wno-unused-variable)
    endif()

    add_test(NAME test_d3d_wrapper COMMAND test_d3d_wrapper)
endif()

# Metal wrapper interception tests
if(MVGAL_BUILD_API)
    add_executable(test_metal_wrapper
        test_metal_wrapper.c
    )

    target_link_libraries(test_metal_wrapper
        PRIVATE
            mvgal_metal
            mvgal_core
            dl
            m
            pthread
    )

    # Suppress unused variable warnings in test file (mock variables for documentation)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
        target_compile_options(test_metal_wrapper PRIVATE -Wno-unused-variable)
    endif()

    add_test(NAME test_metal_wrapper COMMAND test_metal_wrapper)
endif()

# WebGPU wrapper interception tests
if(MVGAL_BUILD_API)
    add_executable(test_webgpu_wrapper_synthetic
        test_webgpu_wrapper_synthetic.c
    )

    target_link_libraries(test_webgpu_wrapper_synthetic
        PRIVATE
            mvgal_webgpu
            mvgal_core
            dl
            m
            pthread
    )

    # Suppress unused variable warnings in test file (mock variables for documentation)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
        target_compile_options(test_webgpu_wrapper_synthetic PRIVATE -Wno-unused-variable)
    endif()

    add_test(NAME test_webgpu_wrapper_synthetic COMMAND test_webgpu_wrapper_synthetic)
endif()
