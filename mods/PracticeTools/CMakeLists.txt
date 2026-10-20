ballance_add_mod(PracticeTools
        SOURCES
        PracticeTools.cpp
        PracticeTools.h)

if(APPLE)
    target_link_libraries(PracticeTools PRIVATE
            "${BML_BALLANCED_ROOT}/build/macos-arm64-runtime/lib/libphysics_RT.dylib"
            "${BML_BALLANCED_ROOT}/build/macos-arm64-runtime/lib/libTT_Toolbox_RT.dylib")
    set_target_properties(PracticeTools PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "@executable_path/../Plugins;@executable_path/../BuildingBlocks;@executable_path/../Bin")
endif()
