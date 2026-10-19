include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/BallanceMod.cmake")

if(APPLE)
    # Native Mods share libc++ and the arm64 ABI with the native loader. Keep
    # this inventory explicit until each legacy Win32 Mod has been ported.
    set(BML_GAMEPLAY_MODS)
    set(BML_TOOLING_MODS PracticeTools SpeedrunShortcuts)
else()
    set(BML_GAMEPLAY_MODS
            BallSticky
            BMLModuls
            DeformedWB
            DualBallControl
            DynamicFov
            MapScripts
            NewSpawn
            SpiritTrail)

    set(BML_TOOLING_MODS
            CameraUtilities
            DebugUtilities
            Segment
            SpeedrunShortcuts
            TravelMode)
endif()

set(BML_MOD_PACKAGES)
foreach(mod IN LISTS BML_GAMEPLAY_MODS)
    ballance_add_mod_directory(${mod} Gameplay package_name)
    list(APPEND BML_MOD_PACKAGES "${package_name}")
endforeach()
foreach(mod IN LISTS BML_TOOLING_MODS)
    ballance_add_mod_directory(${mod} Tooling package_name)
    list(APPEND BML_MOD_PACKAGES "${package_name}")
endforeach()

if(NOT APPLE)
    ballance_add_mod_directory(IVP Providers package_name)
    list(APPEND BML_MOD_PACKAGES "${package_name}")
endif()

list(SORT BML_MOD_PACKAGES)
string(REPLACE ";" "\n" BML_MOD_PACKAGE_LIST "${BML_MOD_PACKAGES}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/mods.txt"
        CONTENT "${BML_MOD_PACKAGE_LIST}\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/mods.txt"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/BML")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/BML/mods")
