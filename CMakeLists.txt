cmake_minimum_required(VERSION 3.25)

project(
    AbyssCursorHide
    VERSION 5.0.1
    DESCRIPTION "A small native Windows utility that hides an idle mouse cursor"
    LANGUAGES CXX RC
)

if(NOT WIN32)
    message(FATAL_ERROR "Abyss Cursor Hide targets Windows only.")
endif()

option(ABYSS_BUILD_TESTS "Build the unit tests" ON)
option(ABYSS_SYSTEM_CURSOR_TESTS
    "Build the tests that change this machine's system cursors (for CI runners)" OFF)

# Strict warnings, SDL checks and the static runtime for every target. Debug
# builds also run MSVC code analysis, and with /WX its warnings fail the build.
function(abyss_msvc_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /WX
            /permissive-
            /utf-8
            /sdl
            $<$<CONFIG:Debug>:/analyze>
        )
        set_property(
            TARGET ${target}
            PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
    endif()
endfunction()

# Exploit mitigations for the two programs that ship. /DEPENDENTLOADFLAG makes
# Windows look for their imported DLLs in System32 only.
function(abyss_program_options target)
    abyss_msvc_options(${target})
    if(MSVC)
        target_compile_options(${target} PRIVATE /guard:cf)
        target_link_options(${target} PRIVATE
            /DYNAMICBASE
            /NXCOMPAT
            /HIGHENTROPYVA
            /guard:cf
            /CETCOMPAT
            /DEPENDENTLOADFLAG:0x800
            /INCREMENTAL:NO
            /MANIFEST:NO
        )
    endif()
endfunction()

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
target_link_libraries(abyss_core PUBLIC advapi32 gdi32 user32)
abyss_msvc_options(abyss_core)
if(MSVC)
    target_compile_options(abyss_core PRIVATE /guard:cf)
endif()

# The version in project() above sets the file properties of both programs and
# the ZIP name; both .rc files read it from this generated header.
# resources/app.manifest has its own copy of the version, updated by hand.
configure_file(resources/version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/version.h" @ONLY)

add_executable(AbyssCursorHide WIN32
    src/main.cpp
    resources/app.rc
)
target_include_directories(AbyssCursorHide PRIVATE
    resources
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
# wtsapi32.dll is the one import that is not a KnownDLL, so it is delay-loaded:
# by the time it loads, wWinMain has limited DLL loading to System32.
target_link_libraries(AbyssCursorHide PRIVATE abyss_core shell32 wtsapi32 delayimp)
abyss_program_options(AbyssCursorHide)
if(MSVC)
    target_link_options(AbyssCursorHide PRIVATE /DELAYLOAD:wtsapi32.dll)
endif()

add_executable(RestoreCursor WIN32
    src/restore_cursor.cpp
    resources/restore.rc
)
target_include_directories(RestoreCursor PRIVATE
    resources
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_link_libraries(RestoreCursor PRIVATE abyss_core)
abyss_program_options(RestoreCursor)

if(ABYSS_BUILD_TESTS)
    enable_testing()

    add_executable(abyss_core_tests tests/core_tests.cpp src/core.cpp)
    target_include_directories(abyss_core_tests PRIVATE include)
    target_compile_features(abyss_core_tests PRIVATE cxx_std_17)
    target_compile_definitions(abyss_core_tests PRIVATE UNICODE _UNICODE NOMINMAX)
    abyss_msvc_options(abyss_core_tests)
    add_test(NAME abyss_core_tests COMMAND abyss_core_tests)

    # Cursor checks that only create private cursors; they never change the
    # system cursors of the machine running them.
    add_executable(abyss_cursor_tests tests/cursor_tests.cpp)
    target_link_libraries(abyss_cursor_tests PRIVATE abyss_core)
    abyss_msvc_options(abyss_cursor_tests)
    add_test(NAME abyss_cursor_tests COMMAND abyss_cursor_tests)

    if(ABYSS_SYSTEM_CURSOR_TESTS)
        # Hides and restores the real system cursors thousands of times and
        # runs the app's --restore and --watch modes. Even when built, it only
        # runs with ABYSS_SYSTEM_CURSOR_TESTS=1 on GitHub Actions; elsewhere it
        # exits with 77, which CTest reports as skipped.
        add_executable(abyss_system_cursor_tests tests/system_cursor_tests.cpp)
        target_link_libraries(abyss_system_cursor_tests PRIVATE abyss_core)
        add_dependencies(abyss_system_cursor_tests AbyssCursorHide)
        abyss_msvc_options(abyss_system_cursor_tests)
        add_test(NAME abyss_system_cursor_tests
            COMMAND abyss_system_cursor_tests $<TARGET_FILE:AbyssCursorHide>)
        set_tests_properties(abyss_system_cursor_tests PROPERTIES
            SKIP_RETURN_CODE 77
            RUN_SERIAL TRUE
            TIMEOUT 900
        )
    endif()
endif()

# The release ZIP holds what someone running the app needs; the rest of the
# documentation stays in the repository.
install(TARGETS AbyssCursorHide RestoreCursor RUNTIME DESTINATION .)
install(FILES README.md LICENSE DESTINATION .)

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME "AbyssCursorHide")
set(CPACK_PACKAGE_VENDOR "Jacob Kozin")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "AbyssCursorHide-${PROJECT_VERSION}-windows-x64")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
include(CPack)
