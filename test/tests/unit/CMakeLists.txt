# Unit Tests CMakeLists.txt

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/mvgal
    ${CMAKE_SOURCE_DIR}/src/userspace
    ${CMAKE_SOURCE_DIR}/src/userspace/api
    ${CMAKE_SOURCE_DIR}/src/userspace/daemon
    ${CMAKE_SOURCE_DIR}/src/userspace/memory
    ${CMAKE_SOURCE_DIR}/src/userspace/scheduler
)

link_directories(
    ${CMAKE_BINARY_DIR}
)

# =============================================================================
# GPU Detection Tests
# =============================================================================

add_executable(test_gpu_detection
    test_gpu_detection.c
)

target_link_libraries(test_gpu_detection
    mvgal_core
    Threads::Threads
    ${DRM_IMPORTED_TARGET}
    ${PCI_IMPORTED_TARGET}
    ${UDEV_IMPORTED_TARGET}
)

add_test(NAME test_gpu_detection COMMAND test_gpu_detection)

# =============================================================================
# Memory Management Tests
# =============================================================================

add_executable(test_memory
    test_memory.c
)

target_link_libraries(test_memory
    mvgal_core
    Threads::Threads
    ${DRM_IMPORTED_TARGET}
    ${PCI_IMPORTED_TARGET}
    ${UDEV_IMPORTED_TARGET}
)

add_test(NAME test_memory COMMAND test_memory)

# =============================================================================
# Scheduler Tests
# =============================================================================

add_executable(test_scheduler
    test_scheduler.c
)

target_link_libraries(test_scheduler
    mvgal_core
    Threads::Threads
    ${DRM_IMPORTED_TARGET}
    ${PCI_IMPORTED_TARGET}
    ${UDEV_IMPORTED_TARGET}
)

add_test(NAME test_scheduler COMMAND test_scheduler)

# =============================================================================
# Core API Tests
# =============================================================================

add_executable(test_core_api
    test_core_api.c
)

target_link_libraries(test_core_api
    mvgal_core
    Threads::Threads
    ${DRM_IMPORTED_TARGET}
    ${PCI_IMPORTED_TARGET}
    ${UDEV_IMPORTED_TARGET}
)

add_test(NAME test_core_api COMMAND test_core_api)

# =============================================================================
# Execution Tests
# =============================================================================

add_executable(test_execution
    test_execution.c
)

target_link_libraries(test_execution
    mvgal_core
    Threads::Threads
    ${DRM_IMPORTED_TARGET}
    ${PCI_IMPORTED_TARGET}
    ${UDEV_IMPORTED_TARGET}
)

add_test(NAME test_execution COMMAND test_execution)

# =============================================================================
# Configuration Tests
# =============================================================================

add_executable(test_config
    test_config.c
)

target_link_libraries(test_config
    mvgal_core
    Threads::Threads
    ${DRM_IMPORTED_TARGET}
    ${PCI_IMPORTED_TARGET}
    ${UDEV_IMPORTED_TARGET}
)

add_test(NAME test_config COMMAND test_config)
