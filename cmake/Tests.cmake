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
