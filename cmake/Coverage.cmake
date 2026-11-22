# Keep instrumentation local to project targets, including inline code exercised
# by test executables. Imported SDK libraries and ordinary builds are unchanged.
if(WXNOTEPAD_ENABLE_COVERAGE)
    get_property(coverage_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS coverage_targets)
        get_target_property(target_type ${target} TYPE)
        if(target_type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|OBJECT_LIBRARY)$")
            target_compile_options(${target} PRIVATE
                --coverage -O0 -g1 -fprofile-abs-path -fprofile-update=atomic)
            if(target_type STREQUAL "EXECUTABLE")
                target_link_options(${target} PRIVATE --coverage)
            endif()
        endif()
    endforeach()
endif()
