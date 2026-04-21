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
        intercept/vulkan/vk_instance.c
        intercept/vulkan/vk_device.c
        intercept/vulkan/vk_queue.c
        intercept/vulkan/vk_command.c
    )

    target_include_directories(mvgal_vulkan_layer
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/../..
            ${Vulkan_INCLUDE_DIRS}
            ${MVGAL_INCLUDE_DIRS}
    )

    target_link_libraries(mvgal_vulkan_layer
        PRIVATE
            mvgal_core
            ${Vulkan_LIBRARIES}
            Threads::Threads
    )

    # Install Vulkan layer
    install(
        TARGETS mvgal_vulkan_layer
        LIBRARY DESTINATION lib/vulkan
        COMPONENT runtime
    )

    # Install Vulkan layer manifest
    install(
        FILES intercept/vulkan/manifest.json
        DESTINATION share/vulkan/explicit_layer.d
        COMPONENT runtime
        RENAME VK_LAYER_MVGAL.json
    )

    # Generate ICD loader json
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/intercept/vulkan/mvgal_icd.json.in"
        "${CMAKE_CURRENT_BINARY_DIR}/mvgal_icd.json"
    )

    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/mvgal_icd.json"
        DESTINATION share/vulkan/icd.d
        COMPONENT runtime
        RENAME mvgal_icd.json
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
