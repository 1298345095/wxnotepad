file(GLOB core_sources CONFIGURE_DEPENDS "${NOTEPAD4_SOURCE}/scintilla/src/*.cxx")
file(GLOB lexlib_sources CONFIGURE_DEPENDS "${NOTEPAD4_SOURCE}/scintilla/lexlib/*.cxx")
add_library(notepad4_core OBJECT ${core_sources} ${lexlib_sources}
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexNull.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexCPP.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexJava.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexCSharp.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexKotlin.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexBatch.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexPowerShell.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexAutoHotkey.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexPython.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexBash.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexRust.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexGo.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexLua.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexMarkdown.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexTOML.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexYAML.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexCMake.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexProps.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexMakefile.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexDiff.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexCSS.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexSQL.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexJSON.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexHTML.cxx"
    "${NOTEPAD4_SOURCE}/scintilla/lexers/LexJavaScript.cxx")
target_include_directories(notepad4_core PUBLIC
    "${NOTEPAD4_SOURCE}/scintilla/include"
    PRIVATE src/editor/platform "${NOTEPAD4_SOURCE}/scintilla/src" "${NOTEPAD4_SOURCE}/scintilla/lexlib")
target_compile_definitions(notepad4_core PUBLIC __WX__ BOOST_REGEX_STANDALONE NO_CXX11_REGEX NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(notepad4_core PUBLIC wx::core wx::base)

# Core objects and their platform symbols share one archive to avoid static link-order cycles.
add_library(editor_runtime STATIC
    src/editor/completion_selection.cpp
    src/editor/platform/latex_input.cpp
    src/editor/caret_appearance.cpp
    src/editor/date_time.cpp
    src/editor/character_information.cpp
    src/editor/title_excerpt.cpp
    src/editor/align_lines.cpp
    src/editor/blank_lines.cpp
    src/editor/bookmarks.cpp
    src/editor/block_comments.cpp
    src/editor/block_navigation.cpp
    src/editor/fold_commands.cpp
    src/editor/selection_commands.cpp
    src/editor/enclose_selection.cpp
    src/editor/url_conversion.cpp
    src/editor/base64_conversion.cpp
    src/editor/rtf_export.cpp
    src/editor/platform/native_encoding.cpp
    src/editor/native_byte_history.cpp
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
    src/editor/auto_insertion.cpp
    src/editor/auto_close_tags.cpp
    src/editor/editor_control.cpp
    src/editor/special_insert.cpp
    src/editor/editor_core.cpp
    src/editor/editor_color_calltip.cpp
    src/editor/platform/clipboard.cpp
    src/editor/platform/custom_case.cpp
    src/editor/platform/character_hex.cpp
    src/editor/platform/escape_text.cpp
    src/editor/platform/sort_collation.cpp
    src/editor/platform/gtk_drag_drop.cpp
    src/editor/platform/wx_platform.cpp)
target_include_directories(editor_runtime PUBLIC src PRIVATE src/editor/platform "${NOTEPAD4_SOURCE}/scintilla/win32"
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
    src/editor/completion_candidates.cpp
    src/editor/word_completion.cpp
    src/editor/completion_styles.cpp
    src/editor/go_to_location.cpp
    src/editor/font_settings.cpp
    src/editor/style_profile_application.cpp
    src/syntax/style_profile.cpp
    src/editor/indentation_settings.cpp
    src/document/document_session.cpp
    src/document/file_variables.cpp
    src/document/encoding/gbk_codec.cpp
    src/document/encoding/single_byte_codec.cpp
    src/document/encoding/chinese_codec.cpp
    src/document/encoding/utf16_codec.cpp
    src/document/encoding/wx_conversion.cpp
    src/document/encoding/encoding_detection.cpp
    src/search/search_session.cpp
    src/syntax/syntax_catalog.cpp
    src/syntax/syntax_scheme.cpp)
# The GBK compatibility codec reuses the pinned Unicode conversion utilities.
target_include_directories(editor_features PRIVATE src/editor/platform "${NOTEPAD4_SOURCE}/scintilla/src")
target_link_libraries(editor_features PUBLIC editor_runtime)

add_library(app_shell STATIC
    src/app/app_icon.cpp
    src/app/dialogs/auto_completion_settings_dialog.cpp
    src/app/dialogs/auto_save_settings_dialog.cpp
    src/app/dialogs/column_wrap_dialog.cpp
    src/app/dialogs/long_line_settings_dialog.cpp
    src/app/dialogs/word_wrap_settings_dialog.cpp
    src/app/dialogs/align_lines_dialog.cpp
    src/app/dialogs/recent_files_dialog.cpp
    src/app/dialogs/sort_lines_dialog.cpp
    src/settings/settings_store.cpp
    src/settings/recent_files.cpp
    src/settings/environment_expansion.cpp
    src/settings/search_settings.cpp
    src/app/dialogs/document_dialogs.cpp
    src/app/dialogs/scheme_chooser.cpp
    src/app/dialogs/enclose_selection_dialog.cpp
    src/app/dialogs/insert_tag_dialog.cpp
    src/app/dialogs/modify_lines_dialog.cpp
    src/app/dialogs/win_edit_input.cpp
    src/app/dialogs/zoom_input.cpp
    src/app/dialogs/print_page_setup_dialog.cpp
    src/app/dialogs/print_page_preview.cpp
    src/app/dialogs/go_to_line_dialog.cpp
    src/app/dialogs/search_feedback_dialog.cpp
    src/app/dialogs/tab_settings_dialog.cpp
    src/app/editor_frame_status.cpp
    src/app/editor_frame_toolbar.cpp
    src/app/editor_frame.cpp
    src/app/file_change_monitor.cpp
    src/app/editor_frame_file_watching.cpp
    src/app/editor_frame_commands.cpp
    src/app/editor_printing.cpp
    src/app/editor_print_job.cpp
    src/app/native_launch.cpp
    src/app/desktop_launch.cpp
    src/app/document_tools.cpp
    src/app/new_window.cpp
    src/app/open_targets.cpp
    src/app/run_command.cpp
    src/app/selection_actions.cpp
    src/app/localization.cpp
    src/app/language_commands.cpp
    src/app/dialogs/find_replace_dialog.cpp
    src/app/dialogs/toolbar_customization_dialog.cpp
    src/app/process_observer.cpp)
target_link_libraries(app_shell PUBLIC editor_features)
if(NOT WIN32)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/app/artwork/wxnotepad.ico" WXNOTEPAD_ICON_HEX HEX)
    string(REGEX REPLACE "(..)" "0x\\1," WXNOTEPAD_ICON_DATA "${WXNOTEPAD_ICON_HEX}")
    configure_file(src/app/app_icon_data.h.in generated/app_icon_data.h @ONLY)
    target_include_directories(app_shell PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
endif()
