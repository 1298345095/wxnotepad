function(add_editor_test target test_name timeout)
    add_gui_program(${target} tests/${target}.cpp app_shell)
    add_test(NAME ${test_name} COMMAND ${target})
    set_tests_properties(${test_name} PROPERTIES TIMEOUT ${timeout})
endfunction()

# CTest order determines private-display shard membership in the CI runner.
# Include these groups in their existing order and append new tests at the end.
include("${CMAKE_CURRENT_LIST_DIR}/tests/Process.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/tests/Application.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/tests/Editor.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/tests/SyntaxAndSearch.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/tests/NativeRegressions.cmake")

add_gui_program(syntax_detection tests/syntax_detection.cpp editor_runtime)
add_test(NAME syntax_detection_contracts COMMAND syntax_detection)
set_tests_properties(syntax_detection_contracts PROPERTIES TIMEOUT 30)

if(WIN32)
    add_test(NAME native_base64_clipboard_error COMMAND "${Python3_EXECUTABLE}" -X utf8
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/base64_clipboard_error.py" "$<TARGET_FILE:base64_conversion>")
    set_tests_properties(native_base64_clipboard_error PROPERTIES TIMEOUT 20)
endif()

add_editor_test(code_format original_code_format 120)
target_compile_definitions(code_format PRIVATE
    "CODE_FORMAT_REFERENCE=u8\"${CMAKE_CURRENT_SOURCE_DIR}/tests/code_format_reference.tsv\""
    "CODE_FORMAT_FILES_REFERENCE=u8\"${CMAKE_CURRENT_SOURCE_DIR}/tests/code_format_files_reference.tsv\"")
add_test(NAME packaged_code_format_localization COMMAND "${Python3_EXECUTABLE}" -X utf8
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/code_format_localization.py" "$<TARGET_FILE:wxnotepad>"
    --cmake-dir "${CMAKE_BINARY_DIR}" --evidence "${CMAKE_BINARY_DIR}/Testing/Temporary/code-format-localization")
set_tests_properties(packaged_code_format_localization PROPERTIES TIMEOUT 120)
add_editor_test(calculate_expression original_calculate_expression 60)
add_test(NAME packaged_calculate_expression COMMAND "${Python3_EXECUTABLE}" -X utf8
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/calculate_expression_packaged.py" "$<TARGET_FILE:wxnotepad>"
    --cmake "${CMAKE_BINARY_DIR}" --evidence "${CMAKE_BINARY_DIR}/Testing/Temporary/calculation-package")
set_tests_properties(packaged_calculate_expression PROPERTIES TIMEOUT 180)

add_gui_program(window_position tests/window_position.cpp app_shell)
add_test(NAME original_window_position COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/managed_x11_session.py" "$<TARGET_FILE:window_position>")
set_tests_properties(original_window_position PROPERTIES TIMEOUT 60)
add_test(NAME original_window_position_process COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/managed_x11_session.py" "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/window_position_process.py" "$<TARGET_FILE:wxnotepad>"
    --cmake-dir "${CMAKE_BINARY_DIR}")
set_tests_properties(original_window_position_process PROPERTIES TIMEOUT 90)
add_test(NAME original_window_position_displays COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/managed_x11_session.py" "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/window_position_displays.py" "$<TARGET_FILE:wxnotepad>")
set_tests_properties(original_window_position_displays PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
