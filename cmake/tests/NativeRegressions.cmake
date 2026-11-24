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
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.27")
    # Preserve the original 60-second guard and retain a stack through the core collector.
    set_tests_properties(original_default_fonts PROPERTIES
        TIMEOUT_SIGNAL_NAME SIGQUIT TIMEOUT_SIGNAL_GRACE_PERIOD 5)
endif()
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
add_dependencies(native_ui_layout wxnotepad_catalogs)
add_gui_program(dialog_layout tests/dialog_layout.cpp app_shell)
add_dependencies(dialog_layout wxnotepad_catalogs)
foreach(language 1033 2052 1028)
    add_test(NAME native_dialog_layout_${language} COMMAND dialog_layout ${language})
    set_tests_properties(native_dialog_layout_${language} PROPERTIES TIMEOUT 60)
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

add_editor_test(settings_store settings_store_ini_semantics 30)
add_editor_test(toolbar_customization original_toolbar_customization 60)
add_dependencies(toolbar_customization wxnotepad_catalogs)
foreach(language 2052 1028)
    add_test(NAME original_toolbar_customization_${language} COMMAND toolbar_customization ${language})
    set_tests_properties(original_toolbar_customization_${language} PROPERTIES TIMEOUT 60)
endforeach()
foreach(configuration malformed clamped unsupported invalid duplicate bounded numeric negative)
    add_test(NAME original_toolbar_${configuration} COMMAND toolbar_customization 1033 ${configuration})
    set_tests_properties(original_toolbar_${configuration} PROPERTIES TIMEOUT 60)
endforeach()

if(WIN32)
    add_executable(clipboard_read_error tests/clipboard_read_error.cpp)
    target_link_libraries(clipboard_read_error PRIVATE wx::base)
    add_test(NAME native_clipboard_read_error_identity COMMAND clipboard_read_error)
    set_tests_properties(native_clipboard_read_error_identity PROPERTIES TIMEOUT 20)
endif()

if(WIN32)
    add_test(NAME native_localization_button_readiness COMMAND "${Python3_EXECUTABLE}" -X utf8
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/localization_button_readiness.py")
    set_tests_properties(native_localization_button_readiness PROPERTIES TIMEOUT 30)
endif()

add_gui_program(extra_escape tests/extra_escape.cpp app_shell)
add_test(NAME original_extra_escape COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/managed_x11_session.py" "$<TARGET_FILE:extra_escape>")
set_tests_properties(original_extra_escape PROPERTIES TIMEOUT 60)
if(WIN32)
    add_gui_program(ime_escape tests/ime_escape.cpp app_shell)
    target_link_libraries(ime_escape PRIVATE imm32 comctl32)
    add_test(NAME windows_pinyin_escape_gdi COMMAND ime_escape 0)
    add_test(NAME windows_pinyin_escape_directwrite COMMAND ime_escape 1)
    set_tests_properties(windows_pinyin_escape_gdi windows_pinyin_escape_directwrite PROPERTIES
        TIMEOUT 45 SKIP_RETURN_CODE 77)
endif()
add_test(NAME original_extra_escape_lifecycle COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/managed_x11_session.py" "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/extra_escape_process.py" "$<TARGET_FILE:extra_escape>")
set_tests_properties(original_extra_escape_lifecycle PROPERTIES TIMEOUT 120)
add_test(NAME packaged_extra_escape_localization COMMAND "${Python3_EXECUTABLE}" -X utf8
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/extra_escape_localization.py" "$<TARGET_FILE:wxnotepad>"
    --cmake-dir "${CMAKE_BINARY_DIR}" --evidence "${CMAKE_BINARY_DIR}/Testing/Temporary/extra-escape-localization")
set_tests_properties(packaged_extra_escape_localization PROPERTIES TIMEOUT 120)

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

add_test(NAME original_scheme_base_filter_reachability COMMAND syntax_save)
set_tests_properties(original_scheme_base_filter_reachability PROPERTIES TIMEOUT 90
    ENVIRONMENT "WXNOTEPAD_BASE_FILTER_REACHABILITY=1")

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

add_executable(diagnostic_print tests/diagnostic_print.cpp tests/completion_allocation_fault.cpp)
target_include_directories(diagnostic_print PRIVATE src)
if(MSVC)
    target_compile_options(diagnostic_print PRIVATE /utf-8)
endif()
add_test(NAME native_print_failure_handling COMMAND "${Python3_EXECUTABLE}" -X utf8
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/diagnostic_print_process.py" "$<TARGET_FILE:diagnostic_print>")
set_tests_properties(native_print_failure_handling PROPERTIES TIMEOUT 30)

add_executable(diagnostic_assert tests/diagnostic_assert.cpp)
target_include_directories(diagnostic_assert PRIVATE src/editor/platform
    "${NOTEPAD4_SOURCE}/scintilla/src" "${NOTEPAD4_SOURCE}/scintilla/lexlib")
target_link_libraries(diagnostic_assert PRIVATE editor_runtime)
add_test(NAME native_assert_output_failure COMMAND "${Python3_EXECUTABLE}" -X utf8
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/diagnostic_assert_process.py" "$<TARGET_FILE:diagnostic_assert>")
set_tests_properties(native_assert_output_failure PROPERTIES TIMEOUT 30)

add_editor_test(style_themes original_style_themes 60)
add_test(NAME original_style_theme_profiles COMMAND style_themes profiles)
set_tests_properties(original_style_theme_profiles PROPERTIES TIMEOUT 60)
if(WIN32)
    add_test(NAME original_style_themes_gdi COMMAND style_themes)
    set_tests_properties(original_style_themes_gdi PROPERTIES TIMEOUT 60 ENVIRONMENT "WXNOTEPAD_THEME_GDI=1")
endif()
add_editor_test(style_customization original_style_customization 90)
add_dependencies(style_customization wxnotepad_catalogs)
foreach(language IN ITEMS 1033 2052 1028)
    add_test(NAME original_style_customization_layout_${language} COMMAND style_customization)
    set_tests_properties(original_style_customization_layout_${language} PROPERTIES TIMEOUT 90
        ENVIRONMENT "WXNOTEPAD_CUSTOMIZE_LANGUAGE=${language}")
endforeach()
if(WIN32)
    add_test(NAME original_style_customization_gdi COMMAND style_customization)
    set_tests_properties(original_style_customization_gdi PROPERTIES TIMEOUT 90 ENVIRONMENT "WXNOTEPAD_CUSTOMIZE_GDI=1")
endif()
