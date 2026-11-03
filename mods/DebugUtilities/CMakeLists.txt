if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.20)
    project(DebugUtilitiesWasm LANGUAGES CXX)
    include("${BALLANCE_WEB_SDK}/BallanceWebMod.cmake")
    ballance_web_mod(DebugUtilities ID DebugUtilities VERSION 0.3.2-web.1
        SOURCES DebugUtilities.cpp DebugCommands.cpp
        RESOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../../LICENSE=LICENSE")
    return()
endif()

ballance_add_mod(DebugUtilities
        SOURCES
        DebugUtilities.cpp
        DebugUtilities.h
        DebugCommands.cpp
        DebugCommands.h)
