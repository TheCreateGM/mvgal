# Vulkan ICD (Installable Client Driver)
# Provides virtual VkPhysicalDevice aggregating all GPUs
# Requires Vulkan 1.3+ headers

if(VULKAN_FOUND OR Vulkan_FOUND)
    # Check if Vulkan headers support 1.3 (needed for VK_API_VERSION_1_3,
    # VkSubmitInfo2, PFN_vkQueueSubmit2, etc.)
    if(VULKAN_INCLUDE_DIRS)
        set(_VK_INC ${VULKAN_INCLUDE_DIRS})
    elseif(Vulkan_INCLUDE_DIRS)
        set(_VK_INC ${Vulkan_INCLUDE_DIRS})
    elseif(Vulkan_INCLUDE_DIR)
        set(_VK_INC ${Vulkan_INCLUDE_DIR})
    else()
        set(_VK_INC "")
    endif()

    if(_VK_INC)
        try_compile(VULKAN_HAS_1_3
            "${CMAKE_BINARY_DIR}/check_vulkan_1_3"
            SOURCES "${CMAKE_SOURCE_DIR}/cmake/check_vulkan_1_3.c"
            CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${_VK_INC}"
        )
    else()
        # Without include dirs, fall back to whether VK_VERSION_1_3 was found
        # via pkg-config (which implies headers are recent enough)
        set(VULKAN_HAS_1_3 ${VULKAN_FOUND})
    endif()

    if(NOT VULKAN_HAS_1_3)
        message(WARNING "Vulkan 1.3 headers not found - Vulkan ICD requires 1.3+, skipping")
        return()
    endif()
    set(VULKAN_ICD_SOURCES
        icd_entry.c
        physical_device.c
        device_group.c
    )

    # Build shared library (ICD must be .so)
    add_library(mvgal_vulkan_icd SHARED ${VULKAN_ICD_SOURCES})

    target_include_directories(mvgal_vulkan_icd PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${VULKAN_INCLUDE_DIRS}
    )

    target_link_libraries(mvgal_vulkan_icd PRIVATE
        mvgal_core
        ${VULKAN_LINK_LIBRARIES}
        pthread
        dl
    )

    set_target_properties(mvgal_vulkan_icd PROPERTIES
        OUTPUT_NAME "mvgal_vulkan_icd"
        PREFIX ""  # No 'lib' prefix for ICD
    )

    # Install ICD library
    install(TARGETS mvgal_vulkan_icd
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )

    # Install ICD manifest JSON
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/mvgal_icd.json.in
        ${CMAKE_CURRENT_BINARY_DIR}/mvgal_icd.json
    )

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/mvgal_icd.json
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/vulkan/icd.d
    )
else()
    message(WARNING "Vulkan not found - Vulkan ICD will not be built")
endif()
