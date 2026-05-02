# MVGAL Qt Monitoring Dashboard
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.16)
project(mvgal-dashboard LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 COMPONENTS Core Widgets Network QUIET)
if(NOT Qt6_FOUND)
    find_package(Qt5 COMPONENTS Core Widgets Network REQUIRED)
    set(QT_VERSION_MAJOR 5)
else()
    set(QT_VERSION_MAJOR 6)
endif()

add_executable(mvgal-dashboard
    mvgal_dashboard.cpp
    mvgal_dashboard.h
)

if(QT_VERSION_MAJOR EQUAL 6)
    target_link_libraries(mvgal-dashboard PRIVATE
        Qt6::Core Qt6::Widgets Qt6::Network)
else()
    target_link_libraries(mvgal-dashboard PRIVATE
        Qt5::Core Qt5::Widgets Qt5::Network)
endif()

install(TARGETS mvgal-dashboard RUNTIME DESTINATION bin)
