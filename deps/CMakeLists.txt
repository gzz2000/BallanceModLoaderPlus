# MinHook is only used by the legacy Windows interception layer. Ballanced
# provides source-level integration points on native platforms.
if (WIN32)
    add_subdirectory(minhook EXCLUDE_FROM_ALL)
endif ()

# zip
add_subdirectory(zip EXCLUDE_FROM_ALL)

# Oniguruma
set(BUILD_SHARED_LIBS OFF CACHE BOOL "")
add_subdirectory(oniguruma EXCLUDE_FROM_ALL)
# Consumers may also include the system regex.h (e.g. GoogleTest on POSIX).
target_compile_definitions(onig INTERFACE ONIG_ESCAPE_REGEX_T_COLLISION)

# utf8.h
add_library(utf8 INTERFACE)
target_include_directories(utf8 INTERFACE ${CMAKE_CURRENT_LIST_DIR}/utf8.h)

# yyjson
add_subdirectory(yyjson EXCLUDE_FROM_ALL)

# ImGui
set(IMGUI_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui PARENT_SCOPE)
