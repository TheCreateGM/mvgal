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

# Helper: set up mvgal_core-dependent tool target
function(add_mvgal_tool TOOL_NAME SOURCE_FILE)
    if(NOT TARGET mvgal_core)
        return()
    endif()
    add_executable(${TOOL_NAME} ${SOURCE_FILE})
    target_include_directories(${TOOL_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
    )
    # RHEL 8 / GCC 8 LTO plugin drops symbols from static archives
    # (libmvgal_core.a) even with --whole-archive.  Disable LTO per-target
    # so mvgal_config_init etc. resolve correctly on these older toolchains.
    # LINK_FLAGS appends after all IPO-generated flags in the link command,
    # so -fno-lto always wins over -flto regardless of GCC version.
    target_compile_options(${TOOL_NAME} PRIVATE -fno-lto)
    set_target_properties(${TOOL_NAME} PROPERTIES LINK_FLAGS "-fno-lto")
    target_link_libraries(${TOOL_NAME} PRIVATE mvgal_core pthread)
    install(TARGETS ${TOOL_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})
endfunction()

# Tools that require libmvgal (linking against the main library)
add_mvgal_tool(mvgal-config mvgal-config.c)

# Special case for 'mvgal' tool to avoid conflict with 'mvgal' library target
add_mvgal_tool(mvgal-bin mvgal.c)
set_target_properties(mvgal-bin PROPERTIES OUTPUT_NAME "mvgal")
