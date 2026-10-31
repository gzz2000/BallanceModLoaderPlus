option(PRACTICE_BUILD_STATIC "Link complete-world practice snapshots into a static source host" OFF)
if(PRACTICE_BUILD_STATIC)
    add_library(PracticeTools STATIC PracticeTools.cpp)
    target_compile_features(PracticeTools PRIVATE cxx_std_20)
    target_include_directories(PracticeTools PRIVATE "${ENGINE_SOURCE_DIR}/RenderEngine/include")
    target_compile_definitions(PracticeTools PRIVATE PRACTICE_BUILD_STATIC=1
        BMLEntry=Practice_Static_Entry BMLExit=Practice_Static_Exit)
    target_link_libraries(PracticeTools PRIVATE BML CK2Static VxMathStatic)
    return()
endif()

ballance_add_mod(PracticeTools
        SOURCES
        PracticeTools.cpp
        PracticeTools.h
        TimelineObjects.h
        OutputTopology.h
        InputTopology.h
    ParameterReferences.h)

if(APPLE)
    target_link_libraries(PracticeTools PRIVATE
            "${_bml_ballanced_build}/lib/libphysics_RT.dylib"
            "${_bml_ballanced_build}/lib/libTT_ParticleSystems_RT.dylib"
            "${_bml_ballanced_build}/lib/libTT_Gravity_RT.dylib"
            "${_bml_ballanced_build}/lib/libTT_Toolbox_RT.dylib")
    set_target_properties(PracticeTools PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "@executable_path/../Plugins;@executable_path/../BuildingBlocks;@executable_path/../Bin")
endif()
