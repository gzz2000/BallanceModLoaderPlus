if (WIN32)
    add_bml_test(VTablePatchTest
            SOURCES
            VTablePatchTest.cpp
            ${BML_SOURCE_DIR}/Hooks/VTablePatch.cpp
            ${BML_SOURCE_DIR}/Hooks/VTablePatch.h
    )

    add_bml_test(RenderHookTest
            SOURCES
            RenderHookTest.cpp
            ${BML_SOURCE_DIR}/Hooks/RenderHook.cpp
            ${BML_SOURCE_DIR}/Hooks/RenderHook.h
            ${BML_SOURCE_DIR}/Hooks/VTablePatch.cpp
            ${BML_SOURCE_DIR}/Hooks/VTablePatch.h
            DEPENDENCIES
            minhook
            BML_HookUtils
            VirtoolsSDK::CK2
            VirtoolsSDK::VxMath
    )
endif ()

if (NOT WIN32)
    add_executable(StaticModRegistryTest
            StaticModRegistryTest.cpp
            ${BML_SOURCE_DIR}/Loader/StaticModRegistry.cpp)
    target_include_directories(StaticModRegistryTest PRIVATE ${BML_SOURCE_DIR} ${BML_INCLUDE_DIR})
    add_test(NAME StaticModRegistryTest COMMAND StaticModRegistryTest)
    add_executable(NativeHookAbiTest
            NativeHookAbiTest.cpp
            ${BML_SOURCE_DIR}/Hooks/VTablePatchPosix.cpp
    )
    target_include_directories(NativeHookAbiTest PRIVATE
            ${BML_SOURCE_DIR}
            ${BML_SOURCE_DIR}/Utils
    )
    target_link_libraries(NativeHookAbiTest PRIVATE VirtoolsSDK::CK2)
    add_test(NAME NativeHookAbiTest COMMAND NativeHookAbiTest)
endif ()

if (NOT WIN32)
    add_executable(NativeRenderSkipTest
            NativeRenderSkipTest.cpp
            ${BML_SOURCE_DIR}/Hooks/RenderHookPosix.cpp
    )
    target_include_directories(NativeRenderSkipTest PRIVATE ${BML_SOURCE_DIR})
    target_link_libraries(NativeRenderSkipTest PRIVATE VirtoolsSDK::CK2)
    add_test(NAME NativeRenderSkipTest COMMAND NativeRenderSkipTest)
endif ()
