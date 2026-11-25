cmake_minimum_required(VERSION 3.25)
project(wxnotepad VERSION 0.1.0 LANGUAGES CXX)
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    # Static application libraries share the dynamic Microsoft C/C++ runtime.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
option(WXNOTEPAD_ENABLE_DIAGNOSTICS "Include opt-in native diagnostic tracing" OFF)
option(WXNOTEPAD_ENABLE_COVERAGE "Collect Linux GCC test coverage" OFF)
option(WXNOTEPAD_FUZZ_ONLY "Build only headless Clang/libFuzzer tests" OFF)
option(WXNOTEPAD_VERIFY_TRIMMED_SDK "Verify the trimmed CI SDK build profile" OFF)
if(WXNOTEPAD_ENABLE_COVERAGE AND NOT
   (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    message(FATAL_ERROR "Coverage requires Linux and GCC")
endif()

include(cmake/Dependencies.cmake)

if(WXNOTEPAD_FUZZ_ONLY)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(FATAL_ERROR "Fuzzing requires Linux and Clang; corpus replay also runs in ordinary builds")
    endif()
    if(WXNOTEPAD_ENABLE_COVERAGE)
        message(FATAL_ERROR "Use separate build directories for fuzzing and GCC coverage")
    endif()
    include(CTest)
    find_package(Python3 3.14 REQUIRED COMPONENTS Interpreter)
    add_subdirectory(tests/fuzz)
    return()
endif()

# Keep the project directory scope for output paths, CTest and coverage.
include(cmake/Targets.cmake)
include(cmake/Platform.cmake)

add_gui_program(wxnotepad src/app/main.cpp app_shell)
add_custom_command(TARGET wxnotepad POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/Notepad4 DarkTheme.ini-default"
        "$<TARGET_FILE_DIR:wxnotepad>/Notepad4 DarkTheme.ini-default"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/Notepad4 DarkTheme.NOTICE.txt"
        "$<TARGET_FILE_DIR:wxnotepad>/Notepad4 DarkTheme.NOTICE.txt"
    VERBATIM)
include(cmake/Localization.cmake)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/Notepad4 DarkTheme.ini-default"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/Notepad4 DarkTheme.NOTICE.txt"
    DESTINATION "${CMAKE_INSTALL_BINDIR}")

include(CTest)
if(BUILD_TESTING)
    include(cmake/Tests.cmake)
endif()

include(cmake/Coverage.cmake)
include(cmake/SizeOptimizations.cmake)
