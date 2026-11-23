cmake_minimum_required(VERSION 3.25)
project(wxnotepad VERSION 0.1.0 LANGUAGES CXX)
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
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
include(cmake/Localization.cmake)

include(CTest)
if(BUILD_TESTING)
    include(cmake/Tests.cmake)
endif()

include(cmake/Coverage.cmake)
include(cmake/SizeOptimizations.cmake)
