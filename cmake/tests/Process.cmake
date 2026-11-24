add_gui_program(run_command tests/run_command.cpp app_shell)
add_executable(run_command_child tests/run_command_child.cpp)
target_link_libraries(run_command_child PRIVATE wx::base)
add_dependencies(run_command run_command_child)
add_test(NAME original_run_command COMMAND run_command)
set_tests_properties(original_run_command PROPERTIES TIMEOUT 90)
add_gui_program(selection_actions tests/selection_actions.cpp app_shell)
add_dependencies(selection_actions run_command_child wxnotepad_catalogs)
add_test(NAME original_selection_actions COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/selection_actions_process.py" "$<TARGET_FILE:selection_actions>")
set_tests_properties(original_selection_actions PROPERTIES TIMEOUT 90)
if(WIN32)
    add_test(NAME original_selection_actions_gdi COMMAND selection_actions --gdi)
    set_tests_properties(original_selection_actions_gdi PROPERTIES TIMEOUT 90)
endif()
add_executable(date_time_format tests/date_time_format.cpp)
target_link_libraries(date_time_format PRIVATE editor_runtime)
if(MSVC)
    target_compile_options(date_time_format PRIVATE /utf-8)
endif()
add_test(NAME original_date_time_literals COMMAND date_time_format)
if(UNIX)
    set_tests_properties(original_date_time_literals PROPERTIES ENVIRONMENT "LC_ALL=C.UTF-8")
endif()
find_package(Python3 3.14 REQUIRED COMPONENTS Interpreter)
add_subdirectory(tests/fuzz)
add_gui_program(sdk_capabilities tests/sdk_capabilities.cpp wx::core wx::base)
target_compile_definitions(sdk_capabilities PRIVATE
    WXNOTEPAD_VERIFY_TRIMMED_SDK=$<BOOL:${WXNOTEPAD_VERIFY_TRIMMED_SDK}>)
add_test(NAME sdk_retained_capabilities COMMAND sdk_capabilities)
set_tests_properties(sdk_retained_capabilities PROPERTIES TIMEOUT 30)
add_test(NAME native_probe_process_runner COMMAND "${Python3_EXECUTABLE}" -X utf8
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ci_probe.py" -v)
set_tests_properties(native_probe_process_runner PROPERTIES TIMEOUT 60
    ENVIRONMENT "WXNOTEPAD_GUI_PROBE=$<TARGET_FILE:sdk_capabilities>;WXNOTEPAD_NATIVE_PROBE_EVIDENCE=${CMAKE_BINARY_DIR}/Testing/Temporary/native-probe")
add_gui_program(default_encoding tests/default_encoding.cpp app_shell)
add_test(NAME original_default_encoding_dialog COMMAND default_encoding)
set_tests_properties(original_default_encoding_dialog PROPERTIES TIMEOUT 60)
add_test(NAME original_default_encoding_settings COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/default_encoding_process.py" "$<TARGET_FILE:default_encoding>")
set_tests_properties(original_default_encoding_settings PROPERTIES TIMEOUT 120)
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
add_test(NAME original_selection_preferences_lifecycle COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/selection_preferences_process.py" "$<TARGET_FILE:selection_options>")
set_tests_properties(original_selection_preferences_lifecycle PROPERTIES TIMEOUT 120)
add_test(NAME original_shebang_settings COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/shebang_settings_process.py" "$<TARGET_FILE:wxnotepad>")
set_tests_properties(original_shebang_settings PROPERTIES TIMEOUT 60)
add_test(NAME original_markup_settings COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/markup_settings_process.py" "$<TARGET_FILE:wxnotepad>")
set_tests_properties(original_markup_settings PROPERTIES TIMEOUT 90)
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
add_test(NAME observed_numeric_menu COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/observer_menu_process.py" "$<TARGET_FILE:wxnotepad>")
set_tests_properties(observed_numeric_menu PROPERTIES TIMEOUT 45)
foreach(scenario startup lifecycle restart native dynamic layout errors base64)
    add_test(NAME packaged_ui_localization_${scenario} COMMAND "${Python3_EXECUTABLE}" -X utf8
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/localization_process.py" "$<TARGET_FILE:wxnotepad>"
        --cmake-dir "${CMAKE_BINARY_DIR}"
        --evidence "${CMAKE_BINARY_DIR}/Testing/Temporary/localization"
        --scenario "${scenario}")
    set_tests_properties(packaged_ui_localization_${scenario} PROPERTIES TIMEOUT 180)
endforeach()
if(NOT WIN32)
    add_test(NAME native_localization_button_readiness COMMAND "${Python3_EXECUTABLE}" -X utf8
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/localization_button_readiness.py")
    set_tests_properties(native_localization_button_readiness PROPERTIES TIMEOUT 30)
    add_test(NAME native_localization_input_readiness COMMAND "${Python3_EXECUTABLE}" -X utf8
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/localization_button_readiness.py" --input)
    set_tests_properties(native_localization_input_readiness PROPERTIES TIMEOUT 30)
    add_test(NAME packaged_ui_localization_regional COMMAND "${Python3_EXECUTABLE}" -X utf8
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/localization_process.py" "$<TARGET_FILE:wxnotepad>"
        --cmake-dir "${CMAKE_BINARY_DIR}"
        --evidence "${CMAKE_BINARY_DIR}/Testing/Temporary/localization"
        --scenario regional)
    set_tests_properties(packaged_ui_localization_regional PROPERTIES TIMEOUT 180)
    add_test(NAME packaged_ui_localization_installed COMMAND "${Python3_EXECUTABLE}" -X utf8
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/localization_process.py" "$<TARGET_FILE:wxnotepad>"
        --cmake-dir "${CMAKE_BINARY_DIR}"
        --evidence "${CMAKE_BINARY_DIR}/Testing/Temporary/localization"
        --scenario installed)
    set_tests_properties(packaged_ui_localization_installed PROPERTIES TIMEOUT 180)
endif()
add_test(NAME observed_process_shutdown COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/observed_shutdown_process.py" "$<TARGET_FILE:wxnotepad>")
set_tests_properties(observed_process_shutdown PROPERTIES TIMEOUT 45)
