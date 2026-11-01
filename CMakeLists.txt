cmake_minimum_required(VERSION 3.25)
project(wxnotepad VERSION 0.1.0 LANGUAGES CXX)
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(wxWidgets 3.3.3 EXACT CONFIG REQUIRED)

set(NOTEPAD4_SOURCE "" CACHE PATH "Pinned, patched source produced by tools/build.py")
if(NOT EXISTS "${NOTEPAD4_SOURCE}/wxnotepad-source.json")
    message(FATAL_ERROR "Use python tools/build.py to prepare the pinned original core first")
endif()
file(READ "${NOTEPAD4_SOURCE}/wxnotepad-source.json" source_record)
string(JSON core_revision GET "${source_record}" revision)
if(NOT core_revision STREQUAL "89b8b36e0ee2c23f73be922315f17261ccfbbe1a")
    message(FATAL_ERROR "Unexpected Notepad4 core revision: ${core_revision}")
endif()

file(GLOB core_sources CONFIGURE_DEPENDS "${NOTEPAD4_SOURCE}/scintilla/src/*.cxx")
file(GLOB lexlib_sources CONFIGURE_DEPENDS "${NOTEPAD4_SOURCE}/scintilla/lexlib/*.cxx")
add_library(notepad4_core OBJECT ${core_sources} ${lexlib_sources}
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexNull.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexCPP.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexPython.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexJSON.cxx")
target_include_directories(notepad4_core PUBLIC
    "${NOTEPAD4_SOURCE}/scintilla/include"
    PRIVATE src/editor/platform "${NOTEPAD4_SOURCE}/scintilla/src" "${NOTEPAD4_SOURCE}/scintilla/lexlib")
target_compile_definitions(notepad4_core PUBLIC __WX__ BOOST_REGEX_STANDALONE NO_CXX11_REGEX NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(notepad4_core PUBLIC wx::core wx::base)

# Core objects and their platform symbols share one archive to avoid static link-order cycles.
add_library(editor_runtime STATIC
    src/editor/editor_control.cpp
    src/editor/platform/wx_platform.cpp)
target_include_directories(editor_runtime PUBLIC src PRIVATE src/editor/platform
    "${NOTEPAD4_SOURCE}/scintilla/src" "${NOTEPAD4_SOURCE}/scintilla/lexlib")
target_link_libraries(editor_runtime PUBLIC notepad4_core)

add_library(editor_features STATIC
    src/editor/line_navigation.cpp
    src/document/document_session.cpp
    src/document/encoding/gbk_codec.cpp
    src/document/encoding/utf16_codec.cpp
    src/search/search_session.cpp
    src/syntax/syntax_catalog.cpp
    src/syntax/syntax_scheme.cpp)
# The GBK compatibility codec reuses the pinned Unicode conversion utilities.
target_include_directories(editor_features PRIVATE "${NOTEPAD4_SOURCE}/scintilla/src")
target_link_libraries(editor_features PUBLIC editor_runtime)

add_library(app_shell STATIC
    src/settings/settings_store.cpp
    src/app/go_to_line_dialog.cpp
    src/app/editor_frame.cpp
    src/app/find_replace_dialog.cpp
    src/app/process_observer.cpp)
target_link_libraries(app_shell PUBLIC editor_features)
if(WIN32)
    target_link_libraries(editor_runtime PRIVATE imm32)
    target_link_libraries(app_shell PRIVATE shell32 ole32)
endif()

# The pinned core predates C++20 char8_t and is compiled without RTTI on Windows.
foreach(target notepad4_core editor_runtime editor_features app_shell)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /GR- /Zc:char8_t-)
    else()
        target_compile_options(${target} PRIVATE -fno-char8_t)
    endif()
endforeach()
if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK REQUIRED IMPORTED_TARGET gtk+-3.0)
    pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-egl wayland-cursor)
    target_link_libraries(editor_runtime PUBLIC PkgConfig::GTK PkgConfig::WAYLAND)
endif()

function(add_gui_program target source)
    add_executable(${target} WIN32 ${source})
    target_link_libraries(${target} PRIVATE ${ARGN})
    if(WIN32)
        target_sources(${target} PRIVATE src/app/windows.rc)
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8)
        # windows.rc embeds wxWidgets' complete DPI-aware application manifest.
        target_link_options(${target} PRIVATE /MANIFEST:NO)
    endif()
endfunction()

add_gui_program(wxnotepad src/app/main.cpp app_shell)

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)
    add_test(NAME original_settings_lifecycle COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/settings_process.py" "$<TARGET_FILE:wxnotepad>")
    set_tests_properties(original_settings_lifecycle PROPERTIES TIMEOUT 60)
    add_test(NAME original_shebang_settings COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/shebang_settings_process.py" "$<TARGET_FILE:wxnotepad>")
    set_tests_properties(original_shebang_settings PROPERTIES TIMEOUT 60)
    function(add_editor_test target test_name timeout)
        add_gui_program(${target} tests/${target}.cpp app_shell)
        add_test(NAME ${test_name} COMMAND ${target})
        set_tests_properties(${test_name} PROPERTIES TIMEOUT ${timeout})
    endfunction()

    # This test reaches feature interfaces without linking the frame or its dialogs.
    add_gui_program(feature_sessions tests/feature_sessions.cpp editor_features)
    add_test(NAME feature_session_contracts COMMAND feature_sessions)
    set_tests_properties(feature_session_contracts PROPERTIES TIMEOUT 30)

    add_gui_program(reopen_document tests/reopen_document.cpp app_shell)
    add_test(NAME reopen_current_document COMMAND reopen_document)
    set_tests_properties(reopen_current_document PROPERTIES TIMEOUT 30)

    add_gui_program(clipboard_peer tests/clipboard_peer.cpp wx::core wx::base)
    add_editor_test(clipboard_smoke system_clipboard 30)
    add_dependencies(clipboard_smoke clipboard_peer)
    add_editor_test(gbk_smoke gbk_roundtrip 30)
    add_editor_test(gbk_native gbk_native_choices 60)
    add_editor_test(utf16_smoke utf16_roundtrip 30)
    add_gui_program(navigation_probe tests/navigation_probe.cpp editor_runtime)
    add_editor_test(editor_smoke editor_input_undo 30)
    add_editor_test(selection_modifiers selection_modifiers 60)
    add_editor_test(editing_keys original_editing_keys 30)
    add_editor_test(block_indentation block_indentation_commands 60)
    add_editor_test(font_smoke native_font_metrics 20)
    add_editor_test(word_wrap word_wrap_view 60)
    add_editor_test(navigation_bindings original_navigation_bindings 60)
    target_include_directories(font_smoke PRIVATE src/editor/platform
        "${NOTEPAD4_SOURCE}/scintilla/src" "${NOTEPAD4_SOURCE}/scintilla/lexlib")
    add_gui_program(display_smoke tests/display_smoke.cpp app_shell)
    add_test(NAME display_repaint COMMAND display_smoke)
    set_tests_properties(display_repaint PROPERTIES TIMEOUT 30)
    if(WIN32)
        add_editor_test(ime_smoke windows_ime_geometry 30)
        target_link_libraries(ime_smoke PRIVATE imm32)
        add_test(NAME windows_dpi_awareness COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/windows_dpi.py" "$<TARGET_FILE:wxnotepad>")
        set_tests_properties(windows_dpi_awareness PROPERTIES TIMEOUT 20)
    endif()
    add_editor_test(file_smoke first_utf8_save 30)
    add_editor_test(close_smoke close_unsaved 30)
    add_editor_test(open_smoke open_utf8 30)
    add_editor_test(syntax_cpp cpp_highlighting 30)
    add_editor_test(syntax_python python_highlighting 30)
    add_editor_test(syntax_shebang original_python_shebang 60)
    add_editor_test(syntax_json json_highlighting 60)
    add_editor_test(syntax_save automatic_language_save 60)
    add_editor_test(syntax_manual manual_language_choice 60)
    add_editor_test(syntax_large large_language_selection 60)
    add_editor_test(fold_cpp cpp_fold_margin 60)
    add_editor_test(search_folds folded_search_replace 60)
    add_editor_test(fold_all fold_all_commands 60)
    add_editor_test(line_numbers line_number_view 60)
    add_editor_test(whitespace_view visible_whitespace 30)
    add_editor_test(indentation_options indentation_options 60)
    add_editor_test(fold_python python_fold_margin 60)
    add_editor_test(fold_json json_fold_margin 60)
    add_editor_test(eol_smoke loaded_eol_input 30)
    add_editor_test(eol_conversion convert_line_endings 60)
    add_editor_test(open_native open_new_native 60)
    add_editor_test(save_smoke save_existing 30)
    add_editor_test(save_native save_as_native 60)
    add_editor_test(search_smoke find_literal_next 30)
    add_editor_test(search_options_smoke find_literal_options 30)
    add_editor_test(replace_smoke replace_literal_one 30)
    add_editor_test(replace_all_smoke replace_literal_all 30)
    add_editor_test(search_navigation find_literal_navigation 30)
    add_editor_test(goto_line go_to_logical_line 30)
    add_editor_test(position_status live_position_selection_status 60)
    add_editor_test(search_regex find_regular_expression 30)
    add_editor_test(replace_regex replace_regular_expression 30)
    add_editor_test(replace_all_regex replace_all_regular_expression 30)
endif()
