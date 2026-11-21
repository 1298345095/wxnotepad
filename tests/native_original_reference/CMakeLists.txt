cmake_minimum_required(VERSION 3.25)
project(native_original_reference LANGUAGES CXX RC)
if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "The independent original control requires native Windows MSVC")
endif()
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
file(GLOB original_sources CONFIGURE_DEPENDS
    "${NOTEPAD4_SOURCE}/scintilla/src/*.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/lexlib/*.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/lexers/*.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/win32/*.cxx")
add_executable(native_original_viewport ${original_sources} main.cpp native_original.rc)
target_include_directories(native_original_viewport PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${NOTEPAD4_SOURCE}/scintilla/include"
    "${NOTEPAD4_SOURCE}/scintilla/src"
    "${NOTEPAD4_SOURCE}/scintilla/lexlib")
target_compile_definitions(native_original_viewport PRIVATE
    UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN
    BOOST_REGEX_STANDALONE NO_CXX11_REGEX _CRT_SECURE_NO_WARNINGS _SCL_SECURE_NO_WARNINGS)
target_compile_options(native_original_viewport PRIVATE /utf-8 /GR- /Zc:char8_t-)
target_link_libraries(native_original_viewport PRIVATE
    user32 gdi32 imm32 shlwapi ole32 oleaut32 shell32 comctl32 uxtheme advapi32 uuid)
target_link_options(native_original_viewport PRIVATE /MANIFEST:NO)

# Compile the pinned helper verbatim, independently of application adapters.
if(NOTEPAD4_OPEN_FOLDER)
    get_filename_component(original_folder_directory "${NOTEPAD4_OPEN_FOLDER}" DIRECTORY)
    add_executable(native_original_open_target open_target.cpp)
    target_include_directories(native_original_open_target PRIVATE "${original_folder_directory}")
    target_compile_definitions(native_original_open_target PRIVATE
        UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN STRICT_TYPED_ITEMIDS)
    target_compile_options(native_original_open_target PRIVATE /utf-8)
    target_link_libraries(native_original_open_target PRIVATE user32 shlwapi ole32 shell32)
endif()
