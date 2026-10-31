# MinHook is only used by the legacy Windows interception layer. Ballanced
# provides source-level integration points on native platforms.
if (WIN32)
    add_subdirectory(minhook EXCLUDE_FROM_ALL)
endif ()

# zip
add_subdirectory(zip EXCLUDE_FROM_ALL)
# zip embeds its own miniz implementation. A static host also links CK2's
# miniz, whose version/ABI need not match. Give zip's private functions a
# namespace instead of allowing the linker to choose between implementations.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/zip/src/miniz.h" _zip_miniz_header)
string(REGEX MATCHALL "(mz_|miniz_|tdefl_|tinfl_)[A-Za-z0-9_]+[ \t\r\n]*\\("
    _zip_miniz_functions "${_zip_miniz_header}")
list(REMOVE_DUPLICATES _zip_miniz_functions)
foreach (_function IN LISTS _zip_miniz_functions)
    string(REGEX REPLACE "[ \t\r\n]*\\($" "" _function "${_function}")
    if (NOT _zip_miniz_header MATCHES "#[ \t]*define[ \t]+${_function}[^A-Za-z0-9_]")
        target_compile_definitions(zip PRIVATE "${_function}=bml_zip_${_function}")
    endif ()
endforeach ()
unset(_zip_miniz_header)
unset(_zip_miniz_functions)

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
