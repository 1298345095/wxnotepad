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
