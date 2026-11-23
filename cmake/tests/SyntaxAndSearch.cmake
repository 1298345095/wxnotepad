add_editor_test(open_smoke open_utf8 30)
add_editor_test(syntax_java_languages original_java_csharp_kotlin_schemes 120)
set(java_language_references
    "JAVA_LANGUAGES_REFERENCE=u8\"${CMAKE_CURRENT_SOURCE_DIR}/tests/data/java_languages_reference.tsv\""
    "JAVA_OPERATIONS_REFERENCE=u8\"${CMAKE_CURRENT_SOURCE_DIR}/tests/data/java_operations_reference.tsv\"")
target_compile_definitions(syntax_java_languages PRIVATE ${java_language_references})
if(WIN32)
    add_test(NAME original_java_csharp_kotlin_dpi96 COMMAND syntax_java_languages)
    set_tests_properties(original_java_csharp_kotlin_dpi96 PROPERTIES TIMEOUT 120 ENVIRONMENT "WXNOTEPAD_JAVA_DPI96=1")
    add_gui_program(directwrite_java_languages tests/syntax_java_languages.cpp app_shell)
    target_compile_definitions(directwrite_java_languages PRIVATE WXNOTEPAD_TEST_DIRECTWRITE ${java_language_references})
    add_test(NAME original_java_csharp_kotlin_directwrite COMMAND directwrite_java_languages)
    set_tests_properties(original_java_csharp_kotlin_directwrite PROPERTIES TIMEOUT 120)
    add_test(NAME original_java_csharp_kotlin_directwrite_dpi96 COMMAND directwrite_java_languages)
    set_tests_properties(original_java_csharp_kotlin_directwrite_dpi96 PROPERTIES TIMEOUT 120
        ENVIRONMENT "WXNOTEPAD_JAVA_DPI96=1")
else()
    add_test(NAME original_java_csharp_kotlin_scale2 COMMAND syntax_java_languages)
    set_tests_properties(original_java_csharp_kotlin_scale2 PROPERTIES TIMEOUT 120 ENVIRONMENT "GDK_SCALE=2;GDK_DPI_SCALE=1")
endif()
add_test(NAME original_java_csharp_kotlin_process COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/java_languages_process.py" "$<TARGET_FILE:wxnotepad>"
    --cmake-dir "${CMAKE_BINARY_DIR}" --evidence "${CMAKE_BINARY_DIR}/Testing/Temporary/java-languages-process")
set_tests_properties(original_java_csharp_kotlin_process PROPERTIES TIMEOUT 180)
add_editor_test(syntax_markdown original_markdown_dialects 90)
target_compile_definitions(syntax_markdown PRIVATE
    "MARKDOWN_REFERENCE=u8\"${CMAKE_CURRENT_SOURCE_DIR}/tests/data/markdown_reference.tsv\"")
add_editor_test(syntax_config_languages original_toml_yaml_cmake_schemes 90)
target_compile_definitions(syntax_config_languages PRIVATE
    "CONFIG_LANGUAGES_REFERENCE=u8\"${CMAKE_CURRENT_SOURCE_DIR}/tests/data/config_languages_reference.tsv\""
    "CONFIG_OPERATIONS_REFERENCE=u8\"${CMAKE_CURRENT_SOURCE_DIR}/tests/data/config_operations_reference.tsv\"")
add_editor_test(syntax_native_languages original_rust_go_lua_schemes 90)
target_compile_definitions(syntax_native_languages PRIVATE
    "NATIVE_LANGUAGES_REFERENCE=u8\"${CMAKE_CURRENT_SOURCE_DIR}/tests/data/native_languages_reference.tsv\"")
add_editor_test(syntax_cpp cpp_highlighting 30)
add_editor_test(syntax_python python_highlighting 30)
add_editor_test(syntax_shell shell_highlighting 60)
add_editor_test(syntax_shebang original_python_shebang 60)
add_editor_test(syntax_json json_highlighting 60)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.27")
    set_tests_properties(json_highlighting PROPERTIES
        TIMEOUT_SIGNAL_NAME SIGQUIT TIMEOUT_SIGNAL_GRACE_PERIOD 5)
endif()
add_editor_test(syntax_javascript javascript_typescript_schemes 120)
add_editor_test(syntax_markup original_markup_schemes 90)
target_compile_definitions(syntax_markup PRIVATE
    "MARKUP_REFERENCE=u8\"${CMAKE_CURRENT_SOURCE_DIR}/tests/data/markup_reference.tsv\"")
add_editor_test(syntax_save automatic_language_save 60)
add_test(NAME original_scheme_base_filter_reachability COMMAND syntax_save)
set_tests_properties(original_scheme_base_filter_reachability PROPERTIES TIMEOUT 90
    ENVIRONMENT "WXNOTEPAD_BASE_FILTER_REACHABILITY=1")
add_editor_test(syntax_manual manual_language_choice 60)
add_editor_test(scheme_chooser original_scheme_chooser 90)
add_editor_test(scheme_filters original_scheme_file_filters 90)
add_dependencies(scheme_chooser wxnotepad_catalogs)
foreach(language 1033 2052 1028)
    add_test(NAME original_scheme_layout_${language} COMMAND scheme_chooser)
    set_tests_properties(original_scheme_layout_${language} PROPERTIES TIMEOUT 90
        ENVIRONMENT "WXNOTEPAD_SCHEME_LAYOUT_LANGUAGE=${language}")
endforeach()
add_test(NAME original_scheme_settings_lifecycle COMMAND "${Python3_EXECUTABLE}" -X utf8
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/scheme_selection_process.py" "$<TARGET_FILE:wxnotepad>")
set_tests_properties(original_scheme_settings_lifecycle PROPERTIES TIMEOUT 90)
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
add_editor_test(fold_commands original_toggle_folds 60)
target_compile_definitions(fold_commands PRIVATE
    "FOLD_COMMAND_REFERENCE=u8\"${CMAKE_CURRENT_SOURCE_DIR}/tests/data/fold_commands_reference.tsv\"")
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
add_editor_test(unicode_controls original_unicode_controls 60)
if(WIN32)
    add_test(NAME original_unicode_controls_dpi96 COMMAND unicode_controls)
    set_tests_properties(original_unicode_controls_dpi96 PROPERTIES TIMEOUT 60
        ENVIRONMENT "WXNOTEPAD_UNICODE_DPI96=1")
else()
    add_test(NAME original_unicode_controls_scale2 COMMAND unicode_controls)
    set_tests_properties(original_unicode_controls_scale2 PROPERTIES TIMEOUT 60
        ENVIRONMENT "GDK_SCALE=2;GDK_DPI_SCALE=1")
endif()
foreach(language en zh_CN zh_TW)
    add_test(NAME original_unicode_controls_process_${language} COMMAND "${Python3_EXECUTABLE}" -X utf8
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/unicode_controls_process.py" "$<TARGET_FILE:wxnotepad>"
        --language "${language}")
    set_tests_properties(original_unicode_controls_process_${language} PROPERTIES TIMEOUT 90)
endforeach()
add_editor_test(line_symbols original_line_symbols 60)
add_test(NAME original_line_symbol_settings COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/line_symbols_process.py" "$<TARGET_FILE:line_symbols>"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/wrap_symbol_reference.tsv")
set_tests_properties(original_line_symbol_settings PROPERTIES TIMEOUT 180)
add_editor_test(indentation_options indentation_options 60)
add_editor_test(automatic_completion original_automatic_completion 120)
add_editor_test(word_completion original_complete_word 60)
add_gui_program(completion_settings tests/completion_settings.cpp app_shell)
add_test(NAME original_completion_settings COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/completion_settings_process.py" "$<TARGET_FILE:completion_settings>")
add_editor_test(completion_popup original_completion_popup 60)
add_editor_test(color_calltip original_color_calltip 90)
if(NOT WIN32)
    add_test(NAME original_color_calltip_scale2 COMMAND color_calltip)
    set_tests_properties(original_color_calltip_scale2 PROPERTIES TIMEOUT 90
        ENVIRONMENT "GDK_SCALE=2;GDK_DPI_SCALE=1")
endif()
add_gui_program(auto_close_tags tests/auto_close_tags.cpp app_shell)
add_test(NAME original_auto_close_tags COMMAND auto_close_tags)
add_gui_program(auto_completion_settings tests/auto_completion_settings.cpp app_shell)
add_test(NAME original_auto_completion_settings COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tests/auto_completion_settings_process.py" "$<TARGET_FILE:auto_completion_settings>")
set_tests_properties(original_auto_completion_settings PROPERTIES TIMEOUT 120)
add_editor_test(auto_insertion original_auto_insertion 90)
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
    set_tests_properties(open_new_native original_markup_schemes PROPERTIES
        TIMEOUT_SIGNAL_NAME SIGQUIT TIMEOUT_SIGNAL_GRACE_PERIOD 5)
endif()
add_editor_test(save_smoke save_existing 30)
add_editor_test(file_change original_file_change_notification 90)
add_editor_test(save_native save_as_native 60)
add_editor_test(search_smoke find_literal_next 30)
add_editor_test(search_wildcards original_wildcard_search 30)
add_editor_test(search_seeding original_find_query_seeding 60)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.27")
    set_tests_properties(original_find_query_seeding PROPERTIES
        TIMEOUT_SIGNAL_NAME SIGQUIT TIMEOUT_SIGNAL_GRACE_PERIOD 5)
endif()
if(WIN32)
    add_dependencies(search_seeding clipboard_peer)
    add_test(NAME native_test_failure_logging COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_test_logging_process.py"
        "$<TARGET_FILE:search_seeding>" "$<TARGET_FILE:clipboard_peer>"
        --evidence "${CMAKE_BINARY_DIR}/Testing/Temporary/native-test-logging")
    set_tests_properties(native_test_failure_logging PROPERTIES TIMEOUT 30)
endif()
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
add_editor_test(shell_appearance original_shell_appearance 60)
add_editor_test(position_status live_position_selection_status 60)
add_editor_test(search_regex find_regular_expression 30)
add_editor_test(replace_regex replace_regular_expression 30)
add_editor_test(replace_all_regex replace_all_regular_expression 30)
add_editor_test(replace_selection replace_in_selection 30)
add_editor_test(search_backslashes original_search_backslashes 30)
add_editor_test(syntax_css_sql original_css_sql_schemes 90)
target_compile_definitions(syntax_css_sql PRIVATE
    "CSS_SQL_REFERENCE=u8\"${CMAKE_CURRENT_SOURCE_DIR}/tests/data/css_sql_reference.tsv\""
    "CSS_SQL_COMPLETION_REFERENCE=u8\"${CMAKE_CURRENT_SOURCE_DIR}/tests/data/css_sql_completion_reference.tsv\""
    "CSS_SQL_TYPING_REFERENCE=u8\"${CMAKE_CURRENT_SOURCE_DIR}/tests/data/css_sql_typing_reference.tsv\"")
foreach(language IN ITEMS en zh_CN zh_TW)
    add_test(NAME original_css_sql_process_${language} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/css_sql_process.py" "$<TARGET_FILE:wxnotepad>" --language "${language}")
    set_tests_properties(original_css_sql_process_${language} PROPERTIES TIMEOUT 120)
endforeach()
