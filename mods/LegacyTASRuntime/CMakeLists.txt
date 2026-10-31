cmake_minimum_required(VERSION 3.20)
project(LegacyTASRuntime LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(ENGINE_SOURCE_DIR "" CACHE PATH "Ballanced Source directory")
set(BML_LIBRARY "" CACHE FILEPATH "BML library containing the instance GUI runtime")
set(CK2_LIBRARY "" CACHE FILEPATH "CK2 library")
set(VXMATH_LIBRARY "" CACHE FILEPATH "VxMath library")
foreach(required ENGINE_SOURCE_DIR BML_LIBRARY CK2_LIBRARY VXMATH_LIBRARY)
    if(NOT EXISTS "${${required}}")
        message(FATAL_ERROR "Set ${required} to an existing path")
    endif()
endforeach()
get_filename_component(BML_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(LEGACY_DIR "${BML_SOURCE_DIR}/mods/LegacyTASRuntime")
add_library(LegacyTASRuntime MODULE LegacyTASRuntime.cpp
    "${LEGACY_DIR}/LegacySegment127/Segment.cpp"
    "${LEGACY_DIR}/LegacySegment127/CommandSeg.cpp"
    "${LEGACY_DIR}/LegacyTASGui/Layout.cpp")
target_include_directories(LegacyTASRuntime PRIVATE
    "${BML_SOURCE_DIR}/include"
    "${LEGACY_DIR}/Legacy0340" "${LEGACY_DIR}/LegacySegment127" "${LEGACY_DIR}/LegacyTASGui"
    "${ENGINE_SOURCE_DIR}/CK2/include" "${ENGINE_SOURCE_DIR}/VxMath/include"
    "${ENGINE_SOURCE_DIR}/BuildingBlocks/TT_InterfaceManager_RT")
target_link_libraries(LegacyTASRuntime PRIVATE "${BML_LIBRARY}" "${CK2_LIBRARY}" "${VXMATH_LIBRARY}")
set_target_properties(LegacyTASRuntime PROPERTIES PREFIX "" SUFFIX ".bmodp")
if(APPLE)
    target_link_options(LegacyTASRuntime PRIVATE
        "-Wl,-rpath,@executable_path/../Plugins" "-Wl,-rpath,@executable_path/../Bin")
endif()
