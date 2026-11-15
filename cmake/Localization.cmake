# The release-language list is shared with runtime selection and packaging.
find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)
set(localization_generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/localization.py"
    header --output "${localization_generated}/ui_language_data.h"
    COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/po/languages.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/localization.py")
target_include_directories(app_shell PUBLIC "${localization_generated}")

include(GNUInstallDirs)
set(WXNOTEPAD_LOCALE_DIR "${CMAKE_INSTALL_FULL_DATADIR}/locale" CACHE PATH "Installed application catalog directory")
target_compile_definitions(app_shell PRIVATE "WXNOTEPAD_LOCALE_DIR=\"${WXNOTEPAD_LOCALE_DIR}\"")

# Host executables never participate in application linkage or runtime DLL copy.
set(WXNOTEPAD_GETTEXT_BIN "$ENV{WXNOTEPAD_GETTEXT_BIN}" CACHE PATH "GNU gettext host executable directory")
if(NOT WXNOTEPAD_GETTEXT_BIN AND VCPKG_INSTALLED_DIR)
    if(VCPKG_HOST_TRIPLET)
        set(gettext_host_triplet "${VCPKG_HOST_TRIPLET}")
    elseif(CMAKE_HOST_WIN32)
        set(gettext_host_triplet "x64-windows")
    else()
        set(gettext_host_triplet "x64-linux")
    endif()
    set(WXNOTEPAD_GETTEXT_BIN "${VCPKG_INSTALLED_DIR}/${gettext_host_triplet}/tools/gettext/bin")
endif()
find_program(WXNOTEPAD_MSGFMT NAMES msgfmt HINTS "${WXNOTEPAD_GETTEXT_BIN}" REQUIRED)
get_filename_component(gettext_bin "${WXNOTEPAD_MSGFMT}" DIRECTORY)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/po/languages.json" release_languages)
string(JSON catalog_domain GET "${release_languages}" domain)
string(JSON source_language GET "${release_languages}" source_language)
string(JSON language_count LENGTH "${release_languages}" languages)
math(EXPR language_last "${language_count} - 1")
set(catalog_outputs)
set(catalog_sources)
foreach(index RANGE ${language_last})
    string(JSON code GET "${release_languages}" languages ${index} code)
    if(code STREQUAL source_language)
        continue()
    endif()
    foreach(domain "${catalog_domain}" wxstd)
        if(domain STREQUAL catalog_domain)
            set(source "${CMAKE_CURRENT_SOURCE_DIR}/po/${code}.po")
        else()
            set(source "${CMAKE_CURRENT_SOURCE_DIR}/po/wxstd/${code}.po")
        endif()
        list(APPEND catalog_sources "${source}")
        list(APPEND catalog_outputs "${CMAKE_CURRENT_BINARY_DIR}/catalogs/locale/${code}/LC_MESSAGES/${domain}.mo")
        install(FILES "${CMAKE_CURRENT_BINARY_DIR}/catalogs/locale/${code}/LC_MESSAGES/${domain}.mo"
            DESTINATION "${WXNOTEPAD_LOCALE_DIR}/${code}/LC_MESSAGES")
    endforeach()
endforeach()
add_custom_command(OUTPUT ${catalog_outputs}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/localization.py"
        --gettext-bin "${gettext_bin}" compile --output "${CMAKE_CURRENT_BINARY_DIR}/catalogs/locale"
    DEPENDS ${catalog_sources} "${CMAKE_CURRENT_SOURCE_DIR}/po/languages.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/localization.py"
    VERBATIM COMMENT "Validating and compiling application and locked wxWidgets catalogs")
# This copy runs even when only a PO changed and the executable needs no relink.
# CMP0112 NEW keeps TARGET_FILE_DIR from introducing a dependency cycle.
add_custom_target(wxnotepad_catalogs ALL
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_BINARY_DIR}/catalogs/locale" "$<TARGET_FILE_DIR:wxnotepad>/locale"
    DEPENDS ${catalog_outputs} VERBATIM)
add_dependencies(wxnotepad wxnotepad_catalogs)
add_custom_target(check_localization
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/localization.py"
        --gettext-bin "${gettext_bin}" check
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" VERBATIM)
install(TARGETS wxnotepad RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/licenses/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/wxnotepad")
