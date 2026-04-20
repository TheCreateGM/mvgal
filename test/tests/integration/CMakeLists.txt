# Integration Tests CMakeLists.txt

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/mvgal
)

link_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../build
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
)

add_test(NAME test_multi_gpu COMMAND test_multi_gpu)
