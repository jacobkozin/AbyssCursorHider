cmake_minimum_required(VERSION 3.25)

project(
    AbyssCursorHide
    VERSION 5.0.0
    DESCRIPTION "A small native Windows utility that hides an idle mouse cursor"
    LANGUAGES CXX RC
)

if(NOT WIN32)
    message(FATAL_ERROR "Abyss Cursor Hide targets Windows only.")
endif()

option(ABYSS_BUILD_TESTS "Build the unit tests" ON)

add_library(abyss_core STATIC
    src/core.cpp
    src/cursor_controller.cpp
    src/windows_settings.cpp
)

target_include_directories(abyss_core PUBLIC include)
target_compile_features(abyss_core PUBLIC cxx_std_17)
target_compile_definitions(abyss_core PUBLIC
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
)
target_link_libraries(abyss_core PUBLIC advapi32 user32)

add_executable(AbyssCursorHide WIN32
    src/main.cpp
    resources/app.rc
)
target_include_directories(AbyssCursorHide PRIVATE resources)
target_link_libraries(AbyssCursorHide PRIVATE abyss_core shell32 wtsapi32)

add_executable(RestoreCursor WIN32
    src/restore_cursor.cpp
    resources/restore.rc
)
target_include_directories(RestoreCursor PRIVATE resources)
target_link_libraries(RestoreCursor PRIVATE abyss_core shell32)

foreach(target abyss_core AbyssCursorHide RestoreCursor)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /WX
            /permissive-
            /utf-8
            /sdl
            /guard:cf
        )
        set_property(
            TARGET ${target}
            PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
    endif()
endforeach()

foreach(target AbyssCursorHide RestoreCursor)
    if(MSVC)
        target_link_options(${target} PRIVATE
            /DYNAMICBASE
            /NXCOMPAT
            /HIGHENTROPYVA
            /guard:cf
            /CETCOMPAT
            /INCREMENTAL:NO
            /MANIFEST:NO
        )
    endif()
endforeach()

if(ABYSS_BUILD_TESTS)
    enable_testing()
    add_executable(abyss_core_tests tests/core_tests.cpp src/core.cpp)
    target_include_directories(abyss_core_tests PRIVATE include)
    target_compile_features(abyss_core_tests PRIVATE cxx_std_17)
    target_compile_definitions(abyss_core_tests PRIVATE UNICODE _UNICODE NOMINMAX)
    if(MSVC)
        target_compile_options(abyss_core_tests PRIVATE /W4 /WX /permissive- /utf-8 /sdl)
        set_property(
            TARGET abyss_core_tests
            PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
    endif()
    add_test(NAME abyss_core_tests COMMAND abyss_core_tests)
endif()

install(TARGETS AbyssCursorHide RestoreCursor RUNTIME DESTINATION .)
install(FILES abyss_cursor_hide.ico README.md LICENSE SECURITY.md DESTINATION .)
install(FILES
    docs/ARCHITECTURE.md
    docs/HISTORY.md
    docs/RELEASING.md
    docs/TESTING.md
    DESTINATION docs
)
install(FILES checksums/SHA256SUMS-v4.17.txt DESTINATION checksums)

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME "AbyssCursorHide")
set(CPACK_PACKAGE_VENDOR "Abyss Cursor Hide")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "AbyssCursorHide-${PROJECT_VERSION}-windows-x64")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
include(CPack)
