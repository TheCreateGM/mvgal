# MVGAL Tools
# SPDX-License-Identifier: MIT

# Subdirectories with their own CMakeLists.txt
add_subdirectory(amd_prototype)
add_subdirectory(libmvgal)
add_subdirectory(monitor)
add_subdirectory(mvgal-probe)
# add_subdirectory(vulkan_pass_through) # Broken stub, using real layer instead

# Standalone tools (no libmvgal dependency)
set(STANDALONE_TOOLS
    mvgal-info
    mvgal-status
    mvgal-bench
    mvgal-compat
    mvgal-steam-setup
)

foreach(tool ${STANDALONE_TOOLS})
    add_executable(${tool} ${tool}.c)
    target_include_directories(${tool} PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/.. 
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
    )
    if(tool STREQUAL "mvgal-bench")
        target_link_libraries(${tool} PRIVATE m pthread)
    elseif(tool STREQUAL "mvgal-steam-setup")
        target_link_libraries(${tool} PRIVATE pthread)
    endif()
    install(TARGETS ${tool} DESTINATION ${CMAKE_INSTALL_BINDIR})
endforeach()

# Tools that require libmvgal (linking against the main library)
set(MVGAL_DEPENDENT_TOOLS
    mvgal-config
)

foreach(tool ${MVGAL_DEPENDENT_TOOLS})
    if(TARGET mvgal_core)
        add_executable(${tool} ${tool}.c)
        target_include_directories(${tool} PRIVATE 
            ${CMAKE_CURRENT_SOURCE_DIR}/.. 
            ${CMAKE_CURRENT_SOURCE_DIR}/../include
        )
        target_link_libraries(${tool} PRIVATE mvgal_core pthread)
        install(TARGETS ${tool} DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endforeach()

# Special case for 'mvgal' tool to avoid conflict with 'mvgal' library target
if(TARGET mvgal_core)
    add_executable(mvgal-bin mvgal.c)
    set_target_properties(mvgal-bin PROPERTIES OUTPUT_NAME "mvgal")
    target_include_directories(mvgal-bin PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/.. 
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
    )
    target_link_libraries(mvgal-bin PRIVATE mvgal_core pthread)
    install(TARGETS mvgal-bin DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
