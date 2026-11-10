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
if(WXNOTEPAD_ENABLE_COVERAGE AND NOT
   (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    message(FATAL_ERROR "Coverage requires Linux and GCC")
endif()
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
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexJSON.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexJavaScript.cxx")
target_include_directories(notepad4_core PUBLIC
    "${NOTEPAD4_SOURCE}/scintilla/include"
    PRIVATE src/editor/platform "${NOTEPAD4_SOURCE}/scintilla/src" "${NOTEPAD4_SOURCE}/scintilla/lexlib")
target_compile_definitions(notepad4_core PUBLIC __WX__ BOOST_REGEX_STANDALONE NO_CXX11_REGEX NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(notepad4_core PUBLIC wx::core wx::base)

# Core objects and their platform symbols share one archive to avoid static link-order cycles.
add_library(editor_runtime STATIC
    src/editor/caret_appearance.cpp
    src/editor/date_time.cpp
    src/editor/align_lines.cpp
    src/editor/blank_lines.cpp
    src/editor/bookmarks.cpp
    src/editor/block_comments.cpp
    src/editor/block_navigation.cpp
    src/editor/enclose_selection.cpp
    src/editor/url_conversion.cpp
    src/editor/compress_whitespace.cpp
    src/editor/sort_lines.cpp
    src/editor/indentation.cpp
    src/editor/join_lines.cpp
    src/editor/line_wrap.cpp
    src/editor/line_comments.cpp
    src/editor/matching_brace.cpp
    src/editor/move_lines.cpp
    src/editor/modify_lines.cpp
    src/editor/strip_blanks.cpp
    src/editor/pad_spaces.cpp
    src/editor/strip_characters.cpp
    src/editor/tab_conversion.cpp
    src/editor/number_radix.cpp
    src/editor/occurrence_marking.cpp
    src/editor/editor_control.cpp
    src/editor/special_insert.cpp
    src/editor/editor_core.cpp
    src/editor/platform/clipboard.cpp
    src/editor/platform/custom_case.cpp
    src/editor/platform/character_hex.cpp
    src/editor/platform/escape_text.cpp
    src/editor/platform/sort_collation.cpp
    src/editor/platform/gtk_drag_drop.cpp
    src/editor/platform/wx_platform.cpp)
target_include_directories(editor_runtime PUBLIC src PRIVATE src/editor/platform
    "${NOTEPAD4_SOURCE}/scintilla/src" "${NOTEPAD4_SOURCE}/scintilla/lexlib")
target_link_libraries(editor_runtime PUBLIC notepad4_core)
if(WXNOTEPAD_ENABLE_DIAGNOSTICS)
    target_sources(editor_runtime PRIVATE src/diagnostics/trace.cpp)
    target_compile_definitions(editor_runtime PUBLIC WXNOTEPAD_DIAGNOSTICS)
    target_compile_definitions(editor_runtime PRIVATE
        "WXNOTEPAD_DIAGNOSTIC_BUILD=\"${PROJECT_VERSION}/$<CONFIG>\""
        "WXNOTEPAD_DIAGNOSTIC_CORE=\"${core_revision}\""
        "WXNOTEPAD_DIAGNOSTIC_COMPILER=\"${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}\"")
endif()

add_library(editor_features STATIC
    src/editor/go_to_location.cpp
    src/editor/indentation_settings.cpp
    src/document/document_session.cpp
    src/document/file_variables.cpp
    src/document/encoding/gbk_codec.cpp
    src/document/encoding/utf16_codec.cpp
    src/document/encoding/encoding_detection.cpp
    src/search/search_session.cpp
    src/syntax/syntax_catalog.cpp
    src/syntax/syntax_scheme.cpp)
# The GBK compatibility codec reuses the pinned Unicode conversion utilities.
target_include_directories(editor_features PRIVATE "${NOTEPAD4_SOURCE}/scintilla/src")
target_link_libraries(editor_features PUBLIC editor_runtime)

add_library(app_shell STATIC
    src/app/dialogs/column_wrap_dialog.cpp
    src/app/dialogs/long_line_settings_dialog.cpp
    src/app/dialogs/align_lines_dialog.cpp
    src/app/dialogs/recent_files_dialog.cpp
    src/app/dialogs/sort_lines_dialog.cpp
    src/settings/settings_store.cpp
    src/settings/recent_files.cpp
    src/settings/search_settings.cpp
    src/app/dialogs/document_dialogs.cpp
    src/app/dialogs/enclose_selection_dialog.cpp
    src/app/dialogs/insert_tag_dialog.cpp
    src/app/dialogs/modify_lines_dialog.cpp
    src/app/dialogs/go_to_line_dialog.cpp
    src/app/dialogs/search_feedback_dialog.cpp
    src/app/dialogs/tab_settings_dialog.cpp
    src/app/editor_frame.cpp
    src/app/file_change_monitor.cpp
    src/app/editor_frame_file_watching.cpp
    src/app/editor_frame_commands.cpp
    src/app/new_window.cpp
    src/app/dialogs/find_replace_dialog.cpp
    src/app/process_observer.cpp)
target_link_libraries(app_shell PUBLIC editor_features)
if(WIN32)
    target_sources(editor_runtime PRIVATE src/editor/platform/windows_ime.cpp)
    target_link_libraries(editor_features PRIVATE advapi32)
    target_link_libraries(editor_runtime PRIVATE imm32 shlwapi ole32)
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
    add_executable(date_time_format tests/date_time_format.cpp)
    target_link_libraries(date_time_format PRIVATE editor_runtime)
    if(MSVC)
        target_compile_options(date_time_format PRIVATE /utf-8)
    endif()
    add_test(NAME original_date_time_literals COMMAND date_time_format)
    if(UNIX)
        set_tests_properties(original_date_time_literals PROPERTIES ENVIRONMENT "LC_ALL=C.UTF-8")
    endif()
    find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)
    set(diagnostic_mode)
    if(NOT WXNOTEPAD_ENABLE_DIAGNOSTICS)
        set(diagnostic_mode --compiled-out)
    endif()
    add_test(NAME native_diagnostic_process COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/diagnostics_process.py" "$<TARGET_FILE:wxnotepad>" ${diagnostic_mode}
        --evidence "${CMAKE_BINARY_DIR}/Testing/Temporary/diagnostics/process")
    set_tests_properties(native_diagnostic_process PROPERTIES TIMEOUT 60)
    add_test(NAME original_settings_lifecycle COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/settings_process.py" "$<TARGET_FILE:wxnotepad>")
    set_tests_properties(original_settings_lifecycle PROPERTIES TIMEOUT 60)
    add_test(NAME original_file_watching_settings COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/file_watching_settings_process.py" "$<TARGET_FILE:wxnotepad>")
    set_tests_properties(original_file_watching_settings PROPERTIES TIMEOUT 60)
    add_gui_program(settings_read_failure tests/settings_read_failure.cpp app_shell)
    add_gui_program(recent_files tests/recent_files.cpp app_shell)
    add_test(NAME original_recent_files COMMAND recent_files)
    set_tests_properties(original_recent_files PROPERTIES TIMEOUT 60)
    add_test(NAME original_recent_files_process COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/recent_files_process.py" "$<TARGET_FILE:wxnotepad>" "$<TARGET_FILE:recent_files>")
    set_tests_properties(original_recent_files_process PROPERTIES TIMEOUT 60)
    add_test(NAME settings_read_failure_preserves_file COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/settings_read_failure_process.py" "$<TARGET_FILE:settings_read_failure>")
    set_tests_properties(settings_read_failure_preserves_file PROPERTIES TIMEOUT 30)
    add_gui_program(selection_options tests/selection_options.cpp app_shell)
    add_test(NAME original_selection_option_gates COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/selection_options_process.py" "$<TARGET_FILE:selection_options>")
    set_tests_properties(original_selection_option_gates PROPERTIES TIMEOUT 60)
    add_test(NAME original_shebang_settings COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/shebang_settings_process.py" "$<TARGET_FILE:wxnotepad>")
    set_tests_properties(original_shebang_settings PROPERTIES TIMEOUT 60)
    add_test(NAME original_encoding_settings COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/encoding_process.py" "$<TARGET_FILE:wxnotepad>")
    set_tests_properties(original_encoding_settings PROPERTIES TIMEOUT 60)
    add_test(NAME original_indentation_settings COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/indentation_process.py" "$<TARGET_FILE:wxnotepad>")
    set_tests_properties(original_indentation_settings PROPERTIES TIMEOUT 60)
    add_test(NAME original_zoom_preference COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/zoom_process.py" "$<TARGET_FILE:wxnotepad>")
    set_tests_properties(original_zoom_preference PROPERTIES TIMEOUT 60)
    add_test(NAME original_current_line_preference COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/current_line_process.py" "$<TARGET_FILE:wxnotepad>")
    set_tests_properties(original_current_line_preference PROPERTIES TIMEOUT 60)
    add_test(NAME original_caret_preference COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/caret_process.py" "$<TARGET_FILE:wxnotepad>")
    set_tests_properties(original_caret_preference PROPERTIES TIMEOUT 60)
    add_test(NAME original_visual_brace_preference COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/visual_brace_process.py" "$<TARGET_FILE:wxnotepad>")
    set_tests_properties(original_visual_brace_preference PROPERTIES TIMEOUT 60)
    add_test(NAME original_line_selection_preference COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/line_selection_process.py" "$<TARGET_FILE:wxnotepad>")
    set_tests_properties(original_line_selection_preference PROPERTIES TIMEOUT 60)
    add_test(NAME observed_process_shutdown COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/observed_shutdown_process.py" "$<TARGET_FILE:wxnotepad>")
    set_tests_properties(observed_process_shutdown PROPERTIES TIMEOUT 45)
    function(add_editor_test target test_name timeout)
        add_gui_program(${target} tests/${target}.cpp app_shell)
        add_test(NAME ${test_name} COMMAND ${target})
        set_tests_properties(${test_name} PROPERTIES TIMEOUT ${timeout})
    endfunction()
    add_gui_program(new_window tests/new_window.cpp app_shell)
    add_test(NAME original_new_window COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/new_window_process.py" "$<TARGET_FILE:new_window>" --scenario normal)
    set_tests_properties(original_new_window PROPERTIES TIMEOUT 45)
    add_test(NAME original_new_window_maximized COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/new_window_process.py" "$<TARGET_FILE:new_window>" --scenario maximized)
    set_tests_properties(original_new_window_maximized PROPERTIES TIMEOUT 45 SKIP_RETURN_CODE 77)
    add_editor_test(menu_hierarchy original_menu_hierarchy 60)
    add_editor_test(date_time original_date_time_insertion 60)
    add_editor_test(caret_appearance original_caret_appearance 60)
    if(WIN32)
        add_test(NAME original_caret_dpi96 COMMAND caret_appearance)
        set_tests_properties(original_caret_dpi96 PROPERTIES TIMEOUT 60
            ENVIRONMENT "WXNOTEPAD_CARET_DPI96=1")
    endif()
    add_editor_test(special_insert original_special_insertion 60)
    add_editor_test(header_insert original_header_insertion 90)
    target_compile_definitions(header_insert PRIVATE
        "HEADER_REFERENCE=u8\"${CMAKE_CURRENT_SOURCE_DIR}/tests/header_reference.tsv\"")
    add_test(NAME original_header_insertion_process COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/header_insert_process.py" "$<TARGET_FILE:wxnotepad>"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/header_saved.tsv")
    set_tests_properties(original_header_insertion_process PROPERTIES TIMEOUT 180)
    target_compile_definitions(special_insert PRIVATE
        "SPECIAL_INSERT_FIXTURES=u8\"${CMAKE_CURRENT_SOURCE_DIR}/tests/special_insert_reference.tsv\""
        "SPECIAL_INSERT_STATES=u8\"${CMAKE_CURRENT_SOURCE_DIR}/tests/special_insert_states.tsv\""
        "SPECIAL_INSERT_SAVED=u8\"${CMAKE_CURRENT_SOURCE_DIR}/tests/special_insert_saved.tsv\"")
    add_test(NAME original_special_insertion_process COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/special_insert_process.py" "$<TARGET_FILE:wxnotepad>"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/special_insert_reference.tsv")
    set_tests_properties(original_special_insertion_process PROPERTIES TIMEOUT 90)
    add_editor_test(path_text original_path_text_insertion 90)
    add_editor_test(read_only original_read_only_mode 120)
    target_compile_definitions(read_only PRIVATE
        "READ_ONLY_REFERENCE_FIXTURES=u8\"${CMAKE_CURRENT_SOURCE_DIR}/tests/read_only_reference.tsv\"")
    add_editor_test(url_conversion original_url_conversion 60)
    target_compile_definitions(url_conversion PRIVATE
        "URL_REFERENCE_FIXTURES=u8\"${CMAKE_CURRENT_SOURCE_DIR}/tests/url_reference_fixtures.tsv\"")
    add_test(NAME original_date_time_settings COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/date_time_process.py" "$<TARGET_FILE:wxnotepad>")
    set_tests_properties(original_date_time_settings PROPERTIES TIMEOUT 90)
    add_editor_test(save_cleanup original_save_cleanup 60)
    add_editor_test(save_copy original_save_copy 90)
    add_editor_test(current_encoding original_current_encoding 90)
    add_test(NAME original_current_encoding_native_keys COMMAND current_encoding)
    set_tests_properties(original_current_encoding_native_keys PROPERTIES TIMEOUT 90
        ENVIRONMENT "WXNOTEPAD_CURRENT_ENCODING_NATIVE_KEYS=1")
    add_editor_test(file_times original_file_times 60)
    add_editor_test(pad_spaces original_pad_spaces 60)
    add_editor_test(align_lines original_align_lines 60)
    add_editor_test(modify_lines original_modify_lines 60)
    add_editor_test(line_wrap original_line_wrap 60)
    add_test(NAME original_wrap_boundary_settings COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/line_wrap_process.py" "$<TARGET_FILE:line_wrap>")
    set_tests_properties(original_wrap_boundary_settings PROPERTIES TIMEOUT 120)
    add_test(NAME original_cleanup_settings COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/cleanup_settings_process.py" "$<TARGET_FILE:wxnotepad>")
    set_tests_properties(original_cleanup_settings PROPERTIES TIMEOUT 60)

    # This test reaches feature interfaces without linking the frame or its dialogs.
    add_gui_program(feature_sessions tests/feature_sessions.cpp editor_features)
    add_test(NAME feature_session_contracts COMMAND feature_sessions)
    set_tests_properties(feature_session_contracts PROPERTIES TIMEOUT 30)

    add_gui_program(reopen_document tests/reopen_document.cpp app_shell)
    add_test(NAME reopen_current_document COMMAND reopen_document)
    set_tests_properties(reopen_current_document PROPERTIES TIMEOUT 30)
    add_gui_program(revert_document tests/revert_document.cpp app_shell)
    add_test(NAME revert_current_document COMMAND revert_document)
    set_tests_properties(revert_current_document PROPERTIES TIMEOUT 60)

    add_gui_program(clipboard_peer tests/clipboard_peer.cpp wx::core wx::base)
    if(UNIX AND NOT APPLE)
        target_link_libraries(clipboard_peer PRIVATE PkgConfig::GTK)
    endif()
    add_editor_test(clipboard_smoke system_clipboard 30)
    add_test(NAME original_read_only_clipboard COMMAND clipboard_smoke)
    set_tests_properties(original_read_only_clipboard PROPERTIES TIMEOUT 120 ENVIRONMENT "WXNOTEPAD_CLIPBOARD_SHAPES=read-only")
    add_test(NAME original_clipboard_shapes COMMAND clipboard_smoke)
    set_tests_properties(original_clipboard_shapes PROPERTIES TIMEOUT 60 ENVIRONMENT "WXNOTEPAD_CLIPBOARD_SHAPES=1")
    add_test(NAME original_current_line_clipboard COMMAND clipboard_smoke)
    set_tests_properties(original_current_line_clipboard PROPERTIES TIMEOUT 90 ENVIRONMENT "WXNOTEPAD_CLIPBOARD_SHAPES=lines")
    add_test(NAME original_line_clipboard_commands COMMAND clipboard_smoke)
    set_tests_properties(original_line_clipboard_commands PROPERTIES TIMEOUT 180 ENVIRONMENT "WXNOTEPAD_CLIPBOARD_SHAPES=commands")
    add_test(NAME original_path_text_clipboard COMMAND clipboard_smoke)
    set_tests_properties(original_path_text_clipboard PROPERTIES TIMEOUT 180 ENVIRONMENT "WXNOTEPAD_CLIPBOARD_SHAPES=path-text")
    add_dependencies(clipboard_smoke clipboard_peer)
    add_test(NAME original_clipboard_utilities COMMAND clipboard_smoke)
    set_tests_properties(original_clipboard_utilities PROPERTIES TIMEOUT 180 ENVIRONMENT "WXNOTEPAD_CLIPBOARD_SHAPES=utilities")
    add_test(NAME original_clipboard_swap COMMAND clipboard_smoke)
    set_tests_properties(original_clipboard_swap PROPERTIES TIMEOUT 180 ENVIRONMENT "WXNOTEPAD_CLIPBOARD_SHAPES=swap")
    add_test(NAME original_clipboard_swap_default_encoding COMMAND clipboard_smoke)
    set_tests_properties(original_clipboard_swap_default_encoding PROPERTIES TIMEOUT 180
        ENVIRONMENT "WXNOTEPAD_CLIPBOARD_SHAPES=swap-encoding")
    add_editor_test(gbk_smoke gbk_roundtrip 30)
    add_editor_test(eol_open mixed_line_ending_open 30)
    add_editor_test(gbk_native gbk_native_choices 60)
    add_editor_test(utf16_smoke utf16_roundtrip 30)
    add_editor_test(encoding_auto automatic_encoding_decisions 30)
    add_gui_program(navigation_probe tests/navigation_probe.cpp editor_runtime)
    add_editor_test(editor_smoke editor_input_undo 30)
    add_gui_program(stream_drag tests/stream_drag.cpp app_shell)
    add_test(NAME native_file_drop COMMAND stream_drag --file-drop)
    set_tests_properties(native_file_drop PROPERTIES TIMEOUT 90)
    add_test(NAME native_stream_drag COMMAND stream_drag)
    set_tests_properties(native_stream_drag PROPERTIES TIMEOUT 90)
    if(UNIX AND NOT APPLE)
        add_test(NAME native_input_request_snapshot COMMAND stream_drag --input-state-probe)
        set_tests_properties(native_input_request_snapshot PROPERTIES TIMEOUT 20)
        add_test(NAME native_peer_geometry_ready COMMAND stream_drag --geometry-probe)
        set_tests_properties(native_peer_geometry_ready PROPERTIES TIMEOUT 20 SKIP_RETURN_CODE 77)
    endif()
    add_editor_test(selection_modifiers selection_modifiers 60)
    add_editor_test(editing_keys original_editing_keys 30)
    add_editor_test(zoom original_zoom_commands 30)
    add_editor_test(current_line original_current_line_highlight 60)
    add_editor_test(bookmark_history original_bookmark_history 90)
    target_compile_definitions(bookmark_history PRIVATE
        "BOOKMARK_HISTORY_REFERENCE=u8\"${CMAKE_CURRENT_SOURCE_DIR}/tests/bookmark_history_reference.tsv\""
        "BOOKMARK_HISTORY_FILES=u8\"${CMAKE_CURRENT_SOURCE_DIR}/tests/bookmark_history_files.tsv\"")
    add_test(NAME original_bookmark_history_preference COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/bookmark_history_process.py" "$<TARGET_FILE:wxnotepad>")
    set_tests_properties(original_bookmark_history_preference PROPERTIES TIMEOUT 90)
    if(WIN32)
        add_test(NAME original_bookmark_history_dpi96 COMMAND bookmark_history)
        set_tests_properties(original_bookmark_history_dpi96 PROPERTIES TIMEOUT 90
            ENVIRONMENT "WXNOTEPAD_BOOKMARK_DPI96=1")
    endif()
    add_editor_test(visual_brace original_visual_brace_matching 90)
    target_include_directories(visual_brace PRIVATE src/editor/platform
        "${NOTEPAD4_SOURCE}/scintilla/src" "${NOTEPAD4_SOURCE}/scintilla/lexlib")
    if(WIN32)
        add_test(NAME original_current_line_dpi96 COMMAND current_line)
        set_tests_properties(original_current_line_dpi96 PROPERTIES TIMEOUT 60
            ENVIRONMENT "WXNOTEPAD_CURRENT_LINE_DPI96=1")
    endif()
    add_editor_test(block_indentation block_indentation_commands 60)
    add_editor_test(tab_conversion original_tab_conversion 60)
    add_editor_test(escape_text original_escape_text 90)
    add_editor_test(number_radix original_number_radix 60)
    add_gui_program(line_comments tests/line_comments.cpp app_shell)
    add_test(NAME original_toggle_line_comment COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/line_comments_process.py" "$<TARGET_FILE:line_comments>")
    set_tests_properties(original_toggle_line_comment PROPERTIES TIMEOUT 90)
    add_editor_test(move_lines original_move_lines 60)
    add_editor_test(block_comments original_toggle_block_comment 60)
    add_editor_test(enclose_selection original_enclose_selection 60)
    add_editor_test(insert_tag original_insert_tag 90)
    target_compile_definitions(insert_tag PRIVATE
        "TAG_REFERENCE_FIXTURES=u8\"${CMAKE_CURRENT_SOURCE_DIR}/tests/tag_reference.tsv\""
        "TAG_STATE_REFERENCE_FIXTURES=u8\"${CMAKE_CURRENT_SOURCE_DIR}/tests/tag_state_reference.tsv\"")
    add_editor_test(strip_blanks original_strip_blanks 60)
    add_editor_test(compress_whitespace original_compress_whitespace 60)
    add_editor_test(blank_lines original_blank_lines 60)
    add_editor_test(strip_characters original_strip_characters 60)
    add_editor_test(join_lines original_join_lines 60)
    add_editor_test(duplicate_lines original_duplicate_lines 60)
    add_editor_test(sort_lines original_sort_lines 90)
    add_editor_test(case_conversion original_case_conversion 60)
    add_editor_test(custom_case original_custom_case 60)
    add_editor_test(character_hex original_character_hex 60)
    add_editor_test(duplicate_transpose original_duplicate_transpose 60)
    add_editor_test(font_smoke native_font_metrics 20)
    add_editor_test(word_wrap word_wrap_view 60)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.27)
        set_tests_properties(word_wrap_view PROPERTIES TIMEOUT_SIGNAL_NAME SIGQUIT)
    endif()
    add_editor_test(navigation_bindings original_navigation_bindings 60)
    add_editor_test(block_navigation original_block_navigation 120)
    add_editor_test(matching_brace original_matching_brace_commands 60)
    target_include_directories(font_smoke PRIVATE src/editor/platform
        "${NOTEPAD4_SOURCE}/scintilla/src" "${NOTEPAD4_SOURCE}/scintilla/lexlib")
    add_gui_program(display_smoke tests/display_smoke.cpp app_shell)
    add_test(NAME display_repaint COMMAND display_smoke)
    set_tests_properties(display_repaint PROPERTIES TIMEOUT 30)
    if(WIN32)
        add_editor_test(ime_smoke windows_ime_geometry 30)
        target_link_libraries(ime_smoke PRIVATE imm32)
        add_editor_test(ime_input windows_inline_ime_input 45)
        target_link_libraries(ime_input PRIVATE imm32 comctl32)
        set_tests_properties(windows_inline_ime_input PROPERTIES SKIP_RETURN_CODE 77)
        add_test(NAME windows_dpi_awareness COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/windows_dpi.py" "$<TARGET_FILE:wxnotepad>")
        set_tests_properties(windows_dpi_awareness PROPERTIES TIMEOUT 20)
    endif()
    add_editor_test(file_smoke first_utf8_save 30)
    add_editor_test(close_smoke close_unsaved 30)
    add_test(NAME native_diagnostic_delivery COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/diagnostics_native_process.py"
        "$<TARGET_FILE:editing_keys>" "$<TARGET_FILE:close_smoke>" ${diagnostic_mode}
        --evidence "${CMAKE_BINARY_DIR}/Testing/Temporary/diagnostics/native")
    set_tests_properties(native_diagnostic_delivery PROPERTIES TIMEOUT 150)
    add_editor_test(open_smoke open_utf8 30)
    add_editor_test(syntax_cpp cpp_highlighting 30)
    add_editor_test(syntax_python python_highlighting 30)
    add_editor_test(syntax_shebang original_python_shebang 60)
    add_editor_test(syntax_json json_highlighting 60)
    add_editor_test(syntax_javascript javascript_typescript_schemes 120)
    add_editor_test(syntax_save automatic_language_save 60)
    add_editor_test(syntax_manual manual_language_choice 60)
    add_editor_test(syntax_large large_language_selection 60)
    if(WXNOTEPAD_ENABLE_COVERAGE)
        # The byte-exact 32 MiB round-trips took 88.5s under CPU contention with
        # coverage instrumentation. Keep the ordinary build's 60s timeout.
        set_tests_properties(large_language_selection PROPERTIES TIMEOUT 120)
        if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.27)
            set_tests_properties(large_language_selection PROPERTIES TIMEOUT_SIGNAL_NAME SIGQUIT)
        endif()
    endif()
    add_editor_test(fold_cpp cpp_fold_margin 60)
    add_editor_test(search_folds folded_search_replace 60)
    add_editor_test(fold_all fold_all_commands 60)
    add_editor_test(fold_view original_folding_view 60)
    if(WIN32)
        add_test(NAME original_folding_view_dpi96 COMMAND fold_view)
        set_tests_properties(original_folding_view_dpi96 PROPERTIES TIMEOUT 60
            ENVIRONMENT "WXNOTEPAD_FOLDING_DPI96=1")
    endif()
    add_test(NAME original_folding_settings COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/folding_view_process.py" "$<TARGET_FILE:wxnotepad>")
    set_tests_properties(original_folding_settings PROPERTIES TIMEOUT 90)
    add_editor_test(line_numbers line_number_view 60)
    add_editor_test(whitespace_view visible_whitespace 30)
    add_editor_test(guides original_guides_and_long_lines 90)
    add_test(NAME original_guides_settings_lifecycle COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/guides_process.py" "$<TARGET_FILE:guides>")
    set_tests_properties(original_guides_settings_lifecycle PROPERTIES TIMEOUT 90)
    add_editor_test(line_symbols original_line_symbols 60)
    add_test(NAME original_line_symbol_settings COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/line_symbols_process.py" "$<TARGET_FILE:line_symbols>"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/wrap_symbol_reference.tsv")
    set_tests_properties(original_line_symbol_settings PROPERTIES TIMEOUT 180)
    add_editor_test(indentation_options indentation_options 60)
    add_editor_test(autoindent original_newline_autoindent 60)
    add_editor_test(file_indentation original_file_indentation 60)
    add_editor_test(fold_python python_fold_margin 60)
    add_editor_test(fold_json json_fold_margin 60)
    add_editor_test(eol_smoke loaded_eol_input 30)
    add_editor_test(eol_conversion convert_line_endings 60)
    add_editor_test(open_native open_new_native 60)
    add_editor_test(reload_encoding reload_encoding_commands 60)
    add_test(NAME reload_encoding_native_keys COMMAND reload_encoding)
    set_tests_properties(reload_encoding_native_keys PROPERTIES TIMEOUT 60
        ENVIRONMENT "WXNOTEPAD_RELOAD_NATIVE_KEYS=1")
    add_editor_test(file_drop file_drop_open 60)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.27")
        # Preserve timeout stacks through the existing Linux core/GDB collector.
        set_tests_properties(open_new_native PROPERTIES
            TIMEOUT_SIGNAL_NAME SIGQUIT TIMEOUT_SIGNAL_GRACE_PERIOD 5)
    endif()
    add_editor_test(save_smoke save_existing 30)
    add_editor_test(file_change original_file_change_notification 90)
    add_editor_test(save_native save_as_native 60)
    add_editor_test(search_smoke find_literal_next 30)
    add_editor_test(search_wildcards original_wildcard_search 30)
    add_editor_test(search_seeding original_find_query_seeding 60)
    add_gui_program(search_settings tests/search_settings.cpp app_shell)
    add_test(NAME original_search_settings COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/search_settings_process.py" "$<TARGET_FILE:search_settings>")
    set_tests_properties(original_search_settings PROPERTIES TIMEOUT 90)
    add_editor_test(find_all original_find_all 30)
    add_editor_test(occurrence_marking original_occurrence_marking 30)
    add_test(NAME original_occurrence_settings COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tests/occurrence_settings_process.py" "$<TARGET_FILE:wxnotepad>")
    set_tests_properties(original_occurrence_settings PROPERTIES TIMEOUT 60)
    add_editor_test(bookmarks original_bookmark_commands 30)
    add_editor_test(search_options_smoke find_literal_options 30)
    add_editor_test(replace_smoke replace_literal_one 30)
    add_editor_test(replace_all_smoke replace_literal_all 30)
    add_editor_test(search_navigation find_literal_navigation 30)
    add_gui_program(search_wrap tests/search_wrap.cpp app_shell)
    add_test(NAME original_search_wrap_feedback COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/search_wrap_process.py" "$<TARGET_FILE:search_wrap>" --feedback)
    set_tests_properties(original_search_wrap_feedback PROPERTIES TIMEOUT 60)
    add_test(NAME original_search_message_settings COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/search_wrap_process.py" "$<TARGET_FILE:search_wrap>")
    set_tests_properties(original_search_message_settings PROPERTIES TIMEOUT 60)
    add_editor_test(goto_line go_to_logical_line 30)
    add_editor_test(position_status live_position_selection_status 60)
    add_editor_test(search_regex find_regular_expression 30)
    add_editor_test(replace_regex replace_regular_expression 30)
    add_editor_test(replace_all_regex replace_all_regular_expression 30)
    add_editor_test(replace_selection replace_in_selection 30)
    add_editor_test(search_backslashes original_search_backslashes 30)
    # Append to retain the existing private-display shard membership and order.
    if(UNIX AND NOT APPLE)
        add_test(NAME native_clipboard_peer_readiness COMMAND clipboard_smoke)
        set_tests_properties(native_clipboard_peer_readiness PROPERTIES TIMEOUT 30
            ENVIRONMENT "WXNOTEPAD_CLIPBOARD_SHAPES=readiness;WXNOTEPAD_CLIPBOARD_DEFER_PEER_RENDER=1;WXNOTEPAD_CLIPBOARD_DEFER_PEER_FOCUS_CLAIM=1")
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_executable(steady_stopwatch tests/steady_stopwatch.cpp)
        target_link_options(steady_stopwatch PRIVATE -Wl,--wrap=clock_gettime -Wl,--wrap=gettimeofday)
        add_test(NAME native_monotonic_wait_clock COMMAND steady_stopwatch)
        set_tests_properties(native_monotonic_wait_clock PROPERTIES TIMEOUT 30)
    endif()
endif()

# Keep instrumentation local to project targets, including inline code exercised
# by test executables. Imported SDK libraries and ordinary builds are unchanged.
if(WXNOTEPAD_ENABLE_COVERAGE)
    get_property(coverage_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS coverage_targets)
        get_target_property(target_type ${target} TYPE)
        if(target_type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|OBJECT_LIBRARY)$")
            target_compile_options(${target} PRIVATE
                --coverage -O0 -g1 -fprofile-abs-path -fprofile-update=atomic)
            if(target_type STREQUAL "EXECUTABLE")
                target_link_options(${target} PRIVATE --coverage)
            endif()
        endif()
    endforeach()
endif()
