add_gui_program(document_tools tests/document_tools.cpp app_shell)
add_test(NAME original_document_tools COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/document_tools_process.py" "$<TARGET_FILE:document_tools>")
set_tests_properties(original_document_tools PROPERTIES TIMEOUT 60)
add_test(NAME packaged_document_tools_localization COMMAND "${Python3_EXECUTABLE}" -X utf8
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/document_tools_localization.py" "$<TARGET_FILE:wxnotepad>"
    --cmake-dir "${CMAKE_BINARY_DIR}" --evidence "${CMAKE_BINARY_DIR}/Testing/Temporary/document-tools-localization")
set_tests_properties(packaged_document_tools_localization PROPERTIES TIMEOUT 120)
if(UNIX)
    target_link_libraries(document_tools PRIVATE PkgConfig::GIO_UNIX)
    add_test(NAME packaged_document_tools_runtime COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/document_tools_process.py" "$<TARGET_FILE:document_tools>"
        --cmake-dir "${CMAKE_BINARY_DIR}")
    set_tests_properties(packaged_document_tools_runtime PROPERTIES TIMEOUT 60)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_gui_program(document_tools_directory_readiness tests/document_tools.cpp app_shell)
        target_sources(document_tools_directory_readiness PRIVATE tests/gtk_directory_load_order.cpp)
        target_compile_definitions(document_tools_directory_readiness PRIVATE WXNOTEPAD_TEST_DIRECTORY_LOAD_ORDER)
        target_link_libraries(document_tools_directory_readiness PRIVATE PkgConfig::GIO_UNIX)
        target_link_options(document_tools_directory_readiness PRIVATE
            -Wl,--wrap=g_file_enumerator_next_files_async -Wl,--wrap=g_file_enumerator_next_files_finish
            -Wl,--wrap=g_file_query_info_async)
        add_test(NAME native_document_tools_directory_readiness COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/document_tools_process.py" "$<TARGET_FILE:document_tools_directory_readiness>")
        set_tests_properties(native_document_tools_directory_readiness PROPERTIES TIMEOUT 60)
    endif()
endif()
add_gui_program(new_window tests/new_window.cpp app_shell)
add_gui_program(open_targets tests/open_targets.cpp app_shell)
add_test(NAME original_open_targets COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/open_targets_process.py" "$<TARGET_FILE:open_targets>")
set_tests_properties(original_open_targets PROPERTIES TIMEOUT 60)
add_test(NAME original_open_target_startup COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/open_target_startup.py" "$<TARGET_FILE:wxnotepad>"
    --cmake-dir "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(original_open_target_startup PROPERTIES TIMEOUT 90)
add_test(NAME original_new_window COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/new_window_process.py" "$<TARGET_FILE:new_window>" --scenario normal)
set_tests_properties(original_new_window PROPERTIES TIMEOUT 45)
add_test(NAME original_new_window_maximized COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/new_window_process.py" "$<TARGET_FILE:new_window>" --scenario maximized)
set_tests_properties(original_new_window_maximized PROPERTIES TIMEOUT 45 SKIP_RETURN_CODE 77)
add_editor_test(scroll_past_last_line original_scroll_past_last_line 60)
add_test(NAME original_scroll_past_last_line_lifecycle COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/scroll_past_last_line_process.py" "$<TARGET_FILE:scroll_past_last_line>")
set_tests_properties(original_scroll_past_last_line_lifecycle PROPERTIES TIMEOUT 120)
add_editor_test(menu_hierarchy original_menu_hierarchy 60)
add_editor_test(context_menu original_editor_context_menu 60)
add_dependencies(context_menu wxnotepad_catalogs)
foreach(language zh_CN zh_TW)
    add_test(NAME original_editor_context_menu_${language} COMMAND context_menu --localized ${language})
    set_tests_properties(original_editor_context_menu_${language} PROPERTIES TIMEOUT 60)
endforeach()
add_editor_test(printing original_printing 90)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    pkg_check_modules(GTK_UNIX_PRINT REQUIRED gtk+-unix-print-3.0)
    target_include_directories(printing PRIVATE ${GTK_UNIX_PRINT_INCLUDE_DIRS})
endif()
add_editor_test(full_screen original_full_screen 60)
add_gui_program(extra_escape tests/extra_escape.cpp app_shell)
add_test(NAME original_extra_escape COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/managed_x11_session.py" "$<TARGET_FILE:extra_escape>")
set_tests_properties(original_extra_escape PROPERTIES TIMEOUT 60)
add_test(NAME original_extra_escape_lifecycle COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/managed_x11_session.py" "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/extra_escape_process.py" "$<TARGET_FILE:extra_escape>")
set_tests_properties(original_extra_escape_lifecycle PROPERTIES TIMEOUT 120)
add_test(NAME packaged_extra_escape_localization COMMAND "${Python3_EXECUTABLE}" -X utf8
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/extra_escape_localization.py" "$<TARGET_FILE:wxnotepad>"
    --cmake-dir "${CMAKE_BINARY_DIR}" --evidence "${CMAKE_BINARY_DIR}/Testing/Temporary/extra-escape-localization")
set_tests_properties(packaged_extra_escape_localization PROPERTIES TIMEOUT 120)
add_test(NAME original_full_screen_settings COMMAND full_screen)
set_tests_properties(original_full_screen_settings PROPERTIES TIMEOUT 60
    ENVIRONMENT "WXNOTEPAD_FULL_SCREEN_SETTINGS=1")
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
add_editor_test(read_only_file original_read_only_file 120)
add_editor_test(read_only original_read_only_mode 120)
target_compile_definitions(read_only PRIVATE
    "READ_ONLY_REFERENCE_FIXTURES=u8\"${CMAKE_CURRENT_SOURCE_DIR}/tests/read_only_reference.tsv\"")
add_editor_test(url_conversion original_url_conversion 60)
add_editor_test(base64_conversion original_base64_conversion 60)
target_compile_definitions(base64_conversion PRIVATE
    "BASE64_REFERENCE=u8\"${CMAKE_CURRENT_SOURCE_DIR}/tests/base64_reference.tsv\"")
target_compile_definitions(url_conversion PRIVATE
    "URL_REFERENCE_FIXTURES=u8\"${CMAKE_CURRENT_SOURCE_DIR}/tests/url_reference_fixtures.tsv\"")
add_test(NAME original_date_time_settings COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/date_time_process.py" "$<TARGET_FILE:wxnotepad>")
set_tests_properties(original_date_time_settings PROPERTIES TIMEOUT 90)
add_editor_test(save_cleanup original_save_cleanup 60)
add_editor_test(save_copy original_save_copy 90)
add_editor_test(save_backup original_save_backup 30)
add_editor_test(periodic_backup original_periodic_backup 90)
add_test(NAME original_periodic_backup_process COMMAND "${Python3_EXECUTABLE}" -X utf8
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/periodic_backup_process.py" "$<TARGET_FILE:wxnotepad>"
    --cmake-dir "${CMAKE_BINARY_DIR}")
set_tests_properties(original_periodic_backup_process PROPERTIES TIMEOUT 180)
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
target_sources(feature_sessions PRIVATE tests/document_load.cpp)
add_test(NAME feature_session_contracts COMMAND feature_sessions)
set_tests_properties(feature_session_contracts PROPERTIES TIMEOUT 30)

add_gui_program(reopen_document tests/reopen_document.cpp app_shell)
add_test(NAME reopen_current_document COMMAND reopen_document)
set_tests_properties(reopen_current_document PROPERTIES TIMEOUT 30)
add_gui_program(revert_document tests/revert_document.cpp app_shell)
add_test(NAME revert_current_document COMMAND revert_document)
set_tests_properties(revert_current_document PROPERTIES TIMEOUT 60)
add_editor_test(window_title original_window_title 60)
target_compile_definitions(window_title PRIVATE
    "WINDOW_TITLE_REFERENCE=u8\"${CMAKE_CURRENT_SOURCE_DIR}/tests/window_title_reference.tsv\"")
add_dependencies(window_title wxnotepad_catalogs)
if(WIN32)
    add_test(NAME original_window_title_native_ansi COMMAND window_title --native-ansi)
    set_tests_properties(original_window_title_native_ansi PROPERTIES TIMEOUT 60)
endif()
foreach(language zh_CN zh_TW)
    add_test(NAME original_window_title_${language} COMMAND window_title --localized ${language})
    set_tests_properties(original_window_title_${language} PROPERTIES TIMEOUT 60)
endforeach()
add_test(NAME original_window_title_process COMMAND "${Python3_EXECUTABLE}" -X utf8
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/window_title_process.py" "$<TARGET_FILE:wxnotepad>")
set_tests_properties(original_window_title_process PROPERTIES TIMEOUT 180)
