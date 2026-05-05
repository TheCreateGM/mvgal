# Vulkan ICD (Installable Client Driver)
# Provides virtual VkPhysicalDevice aggregating all GPUs

set(VULKAN_ICD_SOURCES
    icd_entry.c
    physical_device.c
)

# Build shared library (ICD must be .so)
add_library(mvgal_vulkan_icd SHARED ${VULKAN_ICD_SOURCES})

target_include_directories(mvgal_vulkan_icd PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${VULKAN_INCLUDE_DIR}
)

target_link_libraries(mvgal_vulkan_icd PRIVATE
    mvgal_core
    ${VULKAN_LIBRARY}
    pthread
    dl
)

set_target_properties(mvgal_vulkan_icd PROPERTIES
    OUTPUT_NAME "mvgal_vulkan_icd"
    PREFIX ""  # No 'lib' prefix for ICD
)

# Install ICD library
install(TARGETS mvgal_vulkan_icd
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/mvgal
)

# Install ICD manifest JSON
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/mvgal_icd.json.in
    ${CMAKE_CURRENT_BINARY_DIR}/mvgal_icd.json
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/mvgal_icd.json
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/vulkan/icd.d
)
