add_gui_program(clipboard_peer tests/clipboard_peer.cpp wx::core wx::base)
add_dependencies(context_menu clipboard_peer)
if(UNIX AND NOT APPLE)
    target_link_libraries(clipboard_peer PRIVATE PkgConfig::GTK)
endif()
add_editor_test(clipboard_smoke system_clipboard 30)
target_compile_definitions(clipboard_smoke PRIVATE WXNOTEPAD_RTF_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/rtf")
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
add_editor_test(western_encoding western_encoding 90)
target_compile_definitions(western_encoding PRIVATE
    "WESTERN_ENCODING_FIXTURES=u8\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/encoding_175\"")
add_test(NAME western_encoding_native_keys COMMAND western_encoding)
set_tests_properties(western_encoding_native_keys PROPERTIES TIMEOUT 90
    ENVIRONMENT "WXNOTEPAD_WESTERN_NATIVE_KEYS=1")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.27")
    # Preserve Open As timeout stacks through the existing Linux core collector.
    set_tests_properties(western_encoding western_encoding_native_keys PROPERTIES
        TIMEOUT_SIGNAL_NAME SIGQUIT TIMEOUT_SIGNAL_GRACE_PERIOD 5)
endif()
add_test(NAME western_encoding_suppression COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/western_encoding_process.py" "$<TARGET_FILE:western_encoding>")
set_tests_properties(western_encoding_suppression PROPERTIES TIMEOUT 90)
add_editor_test(chinese_encoding original_chinese_encoding 90)
add_test(NAME original_chinese_encoding_native_keys COMMAND chinese_encoding)
set_tests_properties(original_chinese_encoding_native_keys PROPERTIES TIMEOUT 90
    ENVIRONMENT "WXNOTEPAD_CHINESE_NATIVE_KEYS=1")
add_gui_program(navigation_probe tests/navigation_probe.cpp editor_features)
add_editor_test(editor_smoke editor_input_undo 30)
add_gui_program(stream_drag tests/stream_drag.cpp app_shell)
add_test(NAME native_file_drop COMMAND stream_drag --file-drop)
set_tests_properties(native_file_drop PROPERTIES TIMEOUT 90)
if(WIN32)
    add_test(NAME native_file_drop_delayed_source COMMAND stream_drag --file-drop-delayed-source)
    set_tests_properties(native_file_drop_delayed_source PROPERTIES TIMEOUT 90)
endif()
add_test(NAME native_stream_drag COMMAND stream_drag)
set_tests_properties(native_stream_drag PROPERTIES TIMEOUT 90)
if(UNIX AND NOT APPLE)
    add_test(NAME native_input_request_snapshot COMMAND stream_drag --input-state-probe)
    set_tests_properties(native_input_request_snapshot PROPERTIES TIMEOUT 20)
    add_test(NAME native_peer_geometry_ready COMMAND stream_drag --geometry-probe)
    set_tests_properties(native_peer_geometry_ready PROPERTIES TIMEOUT 20 SKIP_RETURN_CODE 77)
endif()
add_editor_test(selection_modifiers selection_modifiers 60)
add_editor_test(selection_preferences original_selection_preferences 90)
if(WIN32)
    add_test(NAME original_selection_preferences_dpi96 COMMAND selection_preferences)
    set_tests_properties(original_selection_preferences_dpi96 PROPERTIES TIMEOUT 90
        ENVIRONMENT "WXNOTEPAD_SELECTION_DPI96=1")
endif()
target_compile_definitions(selection_preferences PRIVATE
    "SELECTION_PREFERENCES_REFERENCE=u8\"${CMAKE_CURRENT_SOURCE_DIR}/tests/selection_preferences_reference.tsv\"")
add_editor_test(editing_keys original_editing_keys 30)
add_editor_test(zoom original_zoom_commands 30)
if(WIN32)
    target_include_directories(zoom PRIVATE "${NOTEPAD4_SOURCE}/scintilla/win32"
        "${NOTEPAD4_SOURCE}/scintilla/src" "${NOTEPAD4_SOURCE}/scintilla/lexlib")
endif()
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
if(WIN32)
    add_gui_program(character_information tests/character_information.cpp app_shell)
    add_test(NAME original_character_information COMMAND "${Python3_EXECUTABLE}" -X utf8
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/windows_hang_monitor.py"
        --dump-directory "${CMAKE_BINARY_DIR}/Testing/Temporary/character-information-hangs" --dump-after 75
        -- "$<TARGET_FILE:character_information>")
    set_tests_properties(original_character_information PROPERTIES TIMEOUT 90)
else()
    add_editor_test(character_information original_character_information 90)
endif()
add_test(NAME packaged_character_information COMMAND "${Python3_EXECUTABLE}" -X utf8
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/character_information_process.py" "$<TARGET_FILE:wxnotepad>"
    --cmake-dir "${CMAKE_BINARY_DIR}"
    --evidence "${CMAKE_BINARY_DIR}/Testing/Temporary/character-information-package")
set_tests_properties(packaged_character_information PROPERTIES TIMEOUT 120)
if(UNIX AND NOT APPLE)
    find_package(ICU QUIET COMPONENTS uc)
    if(ICU_FOUND)
        string(REGEX MATCH "^[0-9]+" character_metadata_major "${ICU_VERSION}")
        add_library(character_metadata_unavailable SHARED tests/character_metadata_unavailable.cpp)
        set_target_properties(character_metadata_unavailable PROPERTIES OUTPUT_NAME icuuc
            SOVERSION "${character_metadata_major}"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/unavailable-character-metadata")
        add_dependencies(character_information character_metadata_unavailable)
        add_test(NAME original_character_information_no_metadata COMMAND "${CMAKE_COMMAND}" -E env
            "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:character_metadata_unavailable>"
            "WXNOTEPAD_EXPECT_NO_CHARACTER_METADATA=1" "$<TARGET_FILE:character_information>")
        set_tests_properties(original_character_information_no_metadata PROPERTIES TIMEOUT 90)
    endif()
endif()
add_editor_test(duplicate_transpose original_duplicate_transpose 60)
add_editor_test(font_smoke native_font_metrics 20)
add_editor_test(measurement_threads native_parallel_measurement 90)
target_include_directories(measurement_threads PRIVATE src/editor/platform
    "${NOTEPAD4_SOURCE}/scintilla/src" "${NOTEPAD4_SOURCE}/scintilla/lexlib")
find_package(Threads REQUIRED)
add_executable(measurement_worker_start tests/measurement_worker_start.cpp)
target_include_directories(measurement_worker_start PRIVATE src)
target_link_libraries(measurement_worker_start PRIVATE Threads::Threads)
add_test(NAME native_measurement_worker_start COMMAND measurement_worker_start)
set_tests_properties(native_measurement_worker_start PROPERTIES TIMEOUT 20)
add_editor_test(surface_images native_surface_images 20)
target_include_directories(surface_images PRIVATE src/editor/platform
    "${NOTEPAD4_SOURCE}/scintilla/src" "${NOTEPAD4_SOURCE}/scintilla/lexlib")
add_gui_program(word_wrap_settings tests/word_wrap_settings.cpp app_shell)
add_test(NAME original_word_wrap_settings COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/word_wrap_settings_process.py" "$<TARGET_FILE:word_wrap_settings>")
set_tests_properties(original_word_wrap_settings PROPERTIES TIMEOUT 180)
if(WIN32)
    add_test(NAME original_word_wrap_settings_dpi96 COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/word_wrap_settings_process.py" "$<TARGET_FILE:word_wrap_settings>")
    set_tests_properties(original_word_wrap_settings_dpi96 PROPERTIES TIMEOUT 180
        ENVIRONMENT "WXNOTEPAD_WRAP_DPI96=1")
    add_test(NAME original_word_wrap_reference COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/word_wrap_reference_process.py" "$<TARGET_FILE:word_wrap_settings>"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    set_tests_properties(original_word_wrap_reference PROPERTIES TIMEOUT 120)
    add_test(NAME original_word_wrap_reference_dpi96 COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/word_wrap_reference_process.py" "$<TARGET_FILE:word_wrap_settings>"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    set_tests_properties(original_word_wrap_reference_dpi96 PROPERTIES TIMEOUT 120
        ENVIRONMENT "WXNOTEPAD_WRAP_DPI96=1")
endif()
add_editor_test(word_wrap word_wrap_view 60)
if(WIN32)
    foreach(target IN ITEMS clipboard_smoke word_wrap context_menu)
        target_compile_definitions(${target} PRIVATE
            "NATIVE_FAILURE_PYTHON=\"${Python3_EXECUTABLE}\""
            "NATIVE_FAILURE_SCRIPT=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/windows_failure_capture.py\"")
    endforeach()
endif()
add_editor_test(wrap_resize native_wrapped_resize 120)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.27)
    set_tests_properties(word_wrap_view PROPERTIES TIMEOUT_SIGNAL_NAME SIGQUIT)
endif()
add_editor_test(navigation_bindings original_navigation_bindings 60)
add_editor_test(block_navigation original_block_navigation 120)
add_editor_test(selection_commands original_selection_commands 90)
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
