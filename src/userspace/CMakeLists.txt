# Userspace components CMakeLists.txt

# Core library
add_library(mvgal_core STATIC
    api/mvgal_api.c
    api/mvgal_log.c
    daemon/gpu_manager.c
    daemon/config.c
    daemon/ipc.c
    execution/execution.c
    memory/memory.c
    memory/dmabuf.c
    memory/allocator.c
    memory/sync.c
    scheduler/scheduler.c
    scheduler/load_balancer.c
    scheduler/strategy/afr.c
    scheduler/strategy/sfr.c
    scheduler/strategy/task.c
    scheduler/strategy/compute_offload.c
    scheduler/strategy/hybrid.c
)

target_include_directories(mvgal_core
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
        ${MVGAL_INCLUDE_DIRS}
)

target_link_libraries(mvgal_core
    PRIVATE
        Threads::Threads
        ${DRM_IMPORTED_TARGET}
        ${PCI_IMPORTED_TARGET}
        ${UDEV_IMPORTED_TARGET}
        m
)

# Shared library
add_library(mvgal SHARED
    api/mvgal_api.c
    api/mvgal_log.c
    daemon/gpu_manager.c
    daemon/config.c
    daemon/ipc.c
    execution/execution.c
    memory/memory.c
    memory/dmabuf.c
    memory/allocator.c
    memory/sync.c
    scheduler/scheduler.c
    scheduler/load_balancer.c
    scheduler/strategy/afr.c
    scheduler/strategy/sfr.c
    scheduler/strategy/task.c
    scheduler/strategy/compute_offload.c
    scheduler/strategy/hybrid.c
)

target_include_directories(mvgal
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
        ${MVGAL_INCLUDE_DIRS}
)

target_link_libraries(mvgal
    PRIVATE
        mvgal_core
        Threads::Threads
        ${DRM_IMPORTED_TARGET}
        ${PCI_IMPORTED_TARGET}
        ${UDEV_IMPORTED_TARGET}
)

# Vulkan interception layer (optional)
if(WITH_VULKAN AND Vulkan_FOUND)
    add_library(mvgal_vulkan_layer SHARED
        intercept/vulkan/vk_layer.c
    )

    target_include_directories(mvgal_vulkan_layer
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/../..
            ${Vulkan_INCLUDE_DIRS}
    )

    set_target_properties(mvgal_vulkan_layer PROPERTIES
        OUTPUT_NAME "VK_LAYER_MVGAL"
    )

    target_link_options(mvgal_vulkan_layer
        PRIVATE
            -Wl,-Bsymbolic-functions
    )

    target_link_libraries(mvgal_vulkan_layer
        PRIVATE
            Threads::Threads
    )

    file(GENERATE
        OUTPUT "$<TARGET_FILE_DIR:mvgal_vulkan_layer>/VK_LAYER_MVGAL.json"
        CONTENT [=[
{
  "file_format_version": "1.0.0",
  "layer": {
    "name": "VK_LAYER_MVGAL",
    "type": "GLOBAL",
    "library_path": "$<TARGET_FILE:mvgal_vulkan_layer>",
    "api_version": "1.3.0",
    "implementation_version": "1",
    "description": "Milestone 1 Vulkan submit interception layer for MVGAL/UMGAL",
    "functions": {
      "vkNegotiateLoaderLayerInterfaceVersion": "vkNegotiateLoaderLayerInterfaceVersion"
    },
    "enable_environment": {
      "MVGAL_VULKAN_ENABLE": "1"
    },
    "disable_environment": {
      "MVGAL_VULKAN_DISABLE": "1"
    }
  }
}
]=]
    )

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/intercept/vulkan/manifest.json.in"
        "${CMAKE_CURRENT_BINARY_DIR}/VK_LAYER_MVGAL.json"
        @ONLY
    )

    install(
        TARGETS mvgal_vulkan_layer
        LIBRARY DESTINATION lib
        COMPONENT runtime
    )

    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/VK_LAYER_MVGAL.json"
        DESTINATION share/vulkan/implicit_layer.d
        COMPONENT runtime
    )
endif()

# OpenCL interception (optional)
if(WITH_OPENCL AND OpenCL_FOUND)
    add_library(mvgal_opencl SHARED
        intercept/opencl/cl_intercept.c
    )

    target_include_directories(mvgal_opencl
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/../..
            ${OpenCL_INCLUDE_DIRS}
            ${MVGAL_INCLUDE_DIRS}
    )

    target_link_libraries(mvgal_opencl
        PRIVATE
            mvgal_core
            ${OpenCL_LIBRARIES}
            Threads::Threads
            dl
    )

    install(
        TARGETS mvgal_opencl
        LIBRARY DESTINATION lib
        COMPONENT runtime
    )
endif()

# CUDA wrapper (experimental)
if(WITH_CUDA AND CUDAToolkit_FOUND)
    add_library(mvgal_cuda SHARED
        intercept/cuda/cuda_wrapper.c
    )

    target_include_directories(mvgal_cuda
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/../..
            ${CUDAToolkit_INCLUDE_DIRS}
            ${MVGAL_INCLUDE_DIRS}
    )

    target_link_libraries(mvgal_cuda
        PRIVATE
            mvgal_core
            ${CUDAToolkit_LIBRARIES}
            Threads::Threads
            dl
    )

    install(
        TARGETS mvgal_cuda
        LIBRARY DESTINATION lib
        COMPONENT runtime
    )
endif()

# Daemon
if(WITH_DAEMON)
    add_executable(mvgal-daemon
        daemon/main.c
        daemon/gpu_manager.c
        daemon/config.c
        daemon/ipc.c
    )

    target_include_directories(mvgal-daemon
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/../..
            ${MVGAL_INCLUDE_DIRS}
    )

    target_link_libraries(mvgal-daemon
        PRIVATE
            mvgal_core
            Threads::Threads
            ${DRM_IMPORTED_TARGET}
            ${PCI_IMPORTED_TARGET}
            ${UDEV_IMPORTED_TARGET}
    )

    install(
        TARGETS mvgal-daemon
        RUNTIME DESTINATION sbin
        COMPONENT runtime
    )
endif()

# Install core library
install(
    TARGETS mvgal mvgal_core
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    COMPONENT development
)

# Install headers are handled in parent CMakeLists.txt
