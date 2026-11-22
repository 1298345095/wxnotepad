# Append to retain the existing private-display shard membership and order.
if(UNIX AND NOT APPLE)
    add_test(NAME native_clipboard_peer_readiness COMMAND clipboard_smoke)
    set_tests_properties(native_clipboard_peer_readiness PROPERTIES TIMEOUT 30
        ENVIRONMENT "WXNOTEPAD_CLIPBOARD_SHAPES=readiness;WXNOTEPAD_CLIPBOARD_DEFER_PEER_RENDER=1;WXNOTEPAD_CLIPBOARD_DEFER_PEER_FOCUS_CLAIM=1")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_executable(bit_intrinsics tests/bit_intrinsics.cpp)
    target_include_directories(bit_intrinsics PRIVATE src)
    add_test(NAME original_32bit_bit_intrinsics COMMAND bit_intrinsics)
    add_executable(steady_stopwatch tests/steady_stopwatch.cpp)
    target_link_options(steady_stopwatch PRIVATE -Wl,--wrap=clock_gettime -Wl,--wrap=gettimeofday)
    add_test(NAME native_monotonic_wait_clock COMMAND steady_stopwatch)
    set_tests_properties(native_monotonic_wait_clock PROPERTIES TIMEOUT 30)
endif()
if(UNIX AND NOT APPLE)
    add_test(NAME native_file_accept_retry COMMAND syntax_markup)
    set_tests_properties(native_file_accept_retry PROPERTIES TIMEOUT 30
        ENVIRONMENT "WXNOTEPAD_TEST_BLOCK_FILE_ACCEPT=1")
    add_test(NAME native_file_selection_retry COMMAND western_encoding)
    set_tests_properties(native_file_selection_retry PROPERTIES TIMEOUT 30
        ENVIRONMENT "WXNOTEPAD_TEST_LOSE_FILE_SELECTION=1;WXNOTEPAD_WESTERN_NATIVE_KEYS=1")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.27")
        set_tests_properties(native_file_accept_retry native_file_selection_retry PROPERTIES
            TIMEOUT_SIGNAL_NAME SIGQUIT TIMEOUT_SIGNAL_GRACE_PERIOD 5)
    endif()
endif()
# Append native renderer checks to retain existing display shard membership.
if(WIN32)
    add_gui_program(native_directwrite tests/native_directwrite.cpp app_shell)
    target_include_directories(native_directwrite PRIVATE src/editor/platform
        "${NOTEPAD4_SOURCE}/scintilla/win32" "${NOTEPAD4_SOURCE}/scintilla/src"
        "${NOTEPAD4_SOURCE}/scintilla/lexlib")
    target_compile_definitions(native_directwrite PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00)
    add_test(NAME native_directwrite_original_metrics_and_raster COMMAND native_directwrite)
    # The oracle requires unmodified native DWrite parameters; MacType honors
    # this process-local exclusion, without changing the user's desktop profile.
    set_tests_properties(native_directwrite_original_metrics_and_raster PROPERTIES TIMEOUT 90
        ENVIRONMENT "MACTYPE_FORCE_EXCLUDE=1")
    # Replace only the OS contrast query in this test's copy of the production
    # adapter. Native targets/fonts/raster and the pinned oracle remain real.
    add_gui_program(native_directwrite_contrast tests/native_directwrite.cpp app_shell)
    target_sources(native_directwrite_contrast PRIVATE tests/directwrite_contrast_surface.cpp)
    target_include_directories(native_directwrite_contrast PRIVATE src/editor/platform
        "${NOTEPAD4_SOURCE}/scintilla/win32" "${NOTEPAD4_SOURCE}/scintilla/src"
        "${NOTEPAD4_SOURCE}/scintilla/lexlib" "${directwrite_generated}")
    target_compile_definitions(native_directwrite_contrast PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00
        WXNOTEPAD_TEST_CLEARTYPE_CONTRAST)
    add_test(NAME native_directwrite_original_custom_contrast COMMAND native_directwrite_contrast)
    set_tests_properties(native_directwrite_original_custom_contrast PROPERTIES TIMEOUT 90
        ENVIRONMENT "MACTYPE_FORCE_EXCLUDE=1")

    foreach(probe wrap_resize caret_appearance auto_insertion automatic_completion completion_popup
                  unicode_controls color_calltip)
        add_gui_program(directwrite_${probe} tests/${probe}.cpp app_shell)
        target_compile_definitions(directwrite_${probe} PRIVATE WXNOTEPAD_TEST_DIRECTWRITE)
    endforeach()
    add_test(NAME native_directwrite_wrapped_resize COMMAND directwrite_wrap_resize)
    set_tests_properties(native_directwrite_wrapped_resize PROPERTIES TIMEOUT 120)
    add_test(NAME native_directwrite_caret_appearance COMMAND directwrite_caret_appearance)
    set_tests_properties(native_directwrite_caret_appearance PROPERTIES TIMEOUT 60)
    add_test(NAME native_directwrite_caret_dpi96 COMMAND directwrite_caret_appearance)
    set_tests_properties(native_directwrite_caret_dpi96 PROPERTIES TIMEOUT 60 ENVIRONMENT "WXNOTEPAD_CARET_DPI96=1")
    add_test(NAME native_directwrite_unicode_controls COMMAND directwrite_unicode_controls)
    set_tests_properties(native_directwrite_unicode_controls PROPERTIES TIMEOUT 60)
    add_test(NAME native_directwrite_unicode_controls_dpi96 COMMAND directwrite_unicode_controls)
    set_tests_properties(native_directwrite_unicode_controls_dpi96 PROPERTIES TIMEOUT 60
        ENVIRONMENT "WXNOTEPAD_UNICODE_DPI96=1")
    add_test(NAME native_directwrite_auto_insertion COMMAND directwrite_auto_insertion)
    set_tests_properties(native_directwrite_auto_insertion PROPERTIES TIMEOUT 90)
    add_test(NAME native_directwrite_automatic_completion COMMAND directwrite_automatic_completion)
    set_tests_properties(native_directwrite_automatic_completion PROPERTIES TIMEOUT 120)
    add_test(NAME native_directwrite_completion_popup COMMAND directwrite_completion_popup)
    set_tests_properties(native_directwrite_completion_popup PROPERTIES TIMEOUT 60)
    add_test(NAME native_directwrite_color_calltip COMMAND directwrite_color_calltip)
    set_tests_properties(native_directwrite_color_calltip PROPERTIES TIMEOUT 90)
    add_gui_program(native_gdi_resize_viewport tests/native_resize_viewport.cpp app_shell)
    target_compile_definitions(native_gdi_resize_viewport PRIVATE WXNOTEPAD_TEST_GDI)
    add_test(NAME native_gdi_original_resize_viewport COMMAND native_gdi_resize_viewport)
    set_tests_properties(native_gdi_original_resize_viewport PROPERTIES TIMEOUT 120)
    add_gui_program(native_resize_viewport tests/native_resize_viewport.cpp app_shell)
    add_test(NAME native_directwrite_original_resize_viewport COMMAND native_resize_viewport)
    set_tests_properties(native_directwrite_original_resize_viewport PROPERTIES TIMEOUT 120)
    add_gui_program(native_renderer tests/native_renderer.cpp editor_runtime)
    add_test(NAME native_renderer_commands COMMAND native_renderer)
    set_tests_properties(native_renderer_commands PROPERTIES TIMEOUT 60)
endif()

add_editor_test(default_fonts original_default_fonts 60)
if(WIN32)
    target_link_libraries(default_fonts PRIVATE imm32)
    add_test(NAME original_default_fonts_gdi COMMAND default_fonts)
    set_tests_properties(original_default_fonts_gdi PROPERTIES TIMEOUT 60 ENVIRONMENT "WXNOTEPAD_DEFAULT_FONTS_GDI=1")
endif()
add_test(NAME original_default_fonts_lifecycle COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/default_fonts_process.py" "$<TARGET_FILE:default_fonts>")
set_tests_properties(original_default_fonts_lifecycle PROPERTIES TIMEOUT 180)
add_editor_test(rendering_settings original_rendering_settings_menu 60)
if(WIN32)
    target_link_libraries(rendering_settings PRIVATE imm32)
endif()
add_test(NAME original_rendering_settings_lifecycle COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/rendering_settings_process.py" "$<TARGET_FILE:rendering_settings>")
set_tests_properties(original_rendering_settings_lifecycle PROPERTIES TIMEOUT 120)

# Append to preserve the existing private-display shard membership and order.
add_editor_test(completion_allocation native_completion_allocation_failures 60)
target_sources(completion_allocation PRIVATE tests/completion_allocation_fault.cpp)
target_include_directories(completion_allocation PRIVATE src/editor/platform)

add_editor_test(surface_initialization native_surface_initialization_failures 60)
target_sources(surface_initialization PRIVATE tests/completion_allocation_fault.cpp)
target_include_directories(surface_initialization PRIVATE src/editor/platform
    "${NOTEPAD4_SOURCE}/scintilla/src" "${NOTEPAD4_SOURCE}/scintilla/lexlib")

add_executable(parallel_surface_failure tests/parallel_surface_failure.cpp)
target_include_directories(parallel_surface_failure PRIVATE src)
target_link_libraries(parallel_surface_failure PRIVATE Threads::Threads)
target_compile_definitions(parallel_surface_failure PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
add_test(NAME native_parallel_surface_failures COMMAND parallel_surface_failure)
set_tests_properties(native_parallel_surface_failures PROPERTIES TIMEOUT 20)

add_executable(completion_candidates tests/completion_candidates.cpp src/editor/completion_candidates.cpp
    tests/completion_allocation_fault.cpp)
target_include_directories(completion_candidates PRIVATE src)
add_test(NAME completion_candidate_contract COMMAND completion_candidates)
set_tests_properties(completion_candidate_contract PROPERTIES TIMEOUT 20)

add_editor_test(fold_ellipsis original_boxed_fold_ellipsis 90)
if(WIN32)
    add_test(NAME original_boxed_fold_ellipsis_dpi96 COMMAND fold_ellipsis)
    set_tests_properties(original_boxed_fold_ellipsis_dpi96 PROPERTIES TIMEOUT 90
        ENVIRONMENT "WXNOTEPAD_FOLDING_DPI96=1")
else()
    add_test(NAME original_boxed_fold_ellipsis_hidpi COMMAND fold_ellipsis)
    set_tests_properties(original_boxed_fold_ellipsis_hidpi PROPERTIES TIMEOUT 90 ENVIRONMENT "GDK_SCALE=2")
endif()

add_editor_test(bar_visibility original_bar_visibility 60)
add_editor_test(observer_acknowledgement native_observer_unchanged_acknowledgement 45)
add_editor_test(file_change_monitor native_file_watcher_lifetime 30)
add_test(NAME original_bar_visibility_lifecycle COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/bar_visibility_process.py" "$<TARGET_FILE:bar_visibility>")
set_tests_properties(original_bar_visibility_lifecycle PROPERTIES TIMEOUT 120)
if(WIN32)
    add_test(NAME original_hidden_menu_cleanup COMMAND bar_visibility --cleanup)
    set_tests_properties(original_hidden_menu_cleanup PROPERTIES TIMEOUT 15)
endif()

if(WIN32)
    add_editor_test(native_byte_allocation native_byte_allocation_failures 60)
    target_sources(native_byte_allocation PRIVATE tests/completion_allocation_fault.cpp)
    set_tests_properties(native_byte_allocation_failures PROPERTIES SKIP_RETURN_CODE 77)
    add_editor_test(native_byte_load native_byte_load_roundtrip 60)
    set_tests_properties(native_byte_load_roundtrip PROPERTIES SKIP_RETURN_CODE 77)
endif()
if(WIN32)
    add_gui_program(native_byte_information tests/native_byte_information.cpp app_shell)
    add_test(NAME native_invalid_byte_character_information COMMAND "${Python3_EXECUTABLE}" -X utf8
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/windows_hang_monitor.py"
        --dump-directory "${CMAKE_CURRENT_SOURCE_DIR}/.ci/reports/windows-dumps"
        -- "$<TARGET_FILE:native_byte_information>")
    set_tests_properties(native_invalid_byte_character_information PROPERTIES TIMEOUT 60)
    add_test(NAME native_character_info_hang_monitor COMMAND "${Python3_EXECUTABLE}" -X utf8
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_windows_hang_monitor.py" -v)
    set_tests_properties(native_character_info_hang_monitor PROPERTIES TIMEOUT 60
        ENVIRONMENT "WXNOTEPAD_HANG_MONITOR_EVIDENCE=${CMAKE_BINARY_DIR}/Testing/Temporary/character-info-hang-monitor;WXNOTEPAD_GUI_PROBE=$<TARGET_FILE:sdk_capabilities>")
else()
    add_editor_test(native_byte_information native_invalid_byte_character_information 60)
endif()
add_editor_test(search_continuation original_search_continuation 60)
add_test(NAME original_rtf_clipboard COMMAND clipboard_smoke)
set_tests_properties(original_rtf_clipboard PROPERTIES TIMEOUT 60 ENVIRONMENT "WXNOTEPAD_CLIPBOARD_SHAPES=rtf")
if(UNIX AND NOT APPLE)
    # Retain existing registration order; cover native borders with small
    # rendered status text at GTK scale 2 using the same full-paint checks.
    add_test(NAME original_shell_appearance_scale2 COMMAND shell_appearance)
    set_tests_properties(original_shell_appearance_scale2 PROPERTIES TIMEOUT 60
        ENVIRONMENT "GDK_SCALE=2;GDK_DPI_SCALE=0.5")
    add_editor_test(focus_acknowledgement native_focus_acknowledgement 20)
    add_test(NAME native_file_drop_overlap COMMAND stream_drag --file-drop)
    set_tests_properties(native_file_drop_overlap PROPERTIES TIMEOUT 90
        ENVIRONMENT "WXNOTEPAD_FILE_DROP_OVERLAP=1")
    add_test(NAME native_localization_chooser_readiness COMMAND "${Python3_EXECUTABLE}" -X utf8
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/localization_chooser_readiness.py")
    set_tests_properties(native_localization_chooser_readiness PROPERTIES TIMEOUT 30)
    add_test(NAME native_search_feedback_release COMMAND search_continuation)
    set_tests_properties(native_search_feedback_release PROPERTIES TIMEOUT 60
        ENVIRONMENT "WXNOTEPAD_SEARCH_FEEDBACK_RELEASE=1;WXNOTEPAD_NATIVE_KEY_DELAY_MS=150")
    add_test(NAME native_localization_chooser_popup_readiness COMMAND "${Python3_EXECUTABLE}" -X utf8
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/localization_chooser_readiness.py" --popup-delay)
    set_tests_properties(native_localization_chooser_popup_readiness PROPERTIES TIMEOUT 30)
    add_test(NAME native_localization_chooser_role_readiness COMMAND "${Python3_EXECUTABLE}" -X utf8
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/localization_chooser_readiness.py" --role-delay)
    set_tests_properties(native_localization_chooser_role_readiness PROPERTIES TIMEOUT 30)
endif()

add_gui_program(native_ui_layout tests/native_ui_layout.cpp app_shell)
foreach(language 1033 2052 1028)
    add_test(NAME native_replace_layout_${language} COMMAND native_ui_layout replace ${language})
    set_tests_properties(native_replace_layout_${language} PROPERTIES TIMEOUT 30)
    if(UNIX AND NOT APPLE)
        add_test(NAME native_status_baseline_${language} COMMAND native_ui_layout status ${language})
        set_tests_properties(native_status_baseline_${language} PROPERTIES TIMEOUT 30)
        foreach(scale 150 200)
            if(scale EQUAL 150)
                set(font_scale 0.75)
            else()
                set(font_scale 1)
            endif()
            add_test(NAME native_status_baseline_${language}_scale${scale} COMMAND native_ui_layout status ${language})
            set_tests_properties(native_status_baseline_${language}_scale${scale} PROPERTIES TIMEOUT 30
                ENVIRONMENT "GDK_SCALE=2;GDK_DPI_SCALE=${font_scale}")
        endforeach()
    endif()
endforeach()
