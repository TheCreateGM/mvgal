# MVGAL OpenGL Translation Layer
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.16)
project(mvgal-opengl LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_library(DL_LIB dl REQUIRED)
find_package(Threads REQUIRED)

add_library(mvgal_gl SHARED mvgal_gl_preload.c)
target_link_libraries(mvgal_gl PRIVATE ${DL_LIB} Threads::Threads)
target_compile_options(mvgal_gl PRIVATE -Wall -Wextra -O2)

install(TARGETS mvgal_gl LIBRARY DESTINATION lib)
