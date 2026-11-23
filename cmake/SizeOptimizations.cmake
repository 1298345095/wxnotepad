# Keep ordinary Release builds small without changing the editor's optimization
# level. Coverage retains its original instrumentation and link behaviour.
option(WXNOTEPAD_ENABLE_SIZE_OPTIMIZATIONS "Discard unused Release code and compact Linux relocations" ON)
option(WXNOTEPAD_ENABLE_IPO "Enable Release interprocedural optimization (slower links)" OFF)

if(WXNOTEPAD_ENABLE_COVERAGE)
    if(WXNOTEPAD_ENABLE_IPO)
        message(FATAL_ERROR "Use a separate build directory for IPO and coverage")
    endif()
    return()
endif()

set(size_configuration "$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>")
set(application_targets notepad4_core editor_runtime editor_features app_shell wxnotepad)

if(WXNOTEPAD_ENABLE_SIZE_OPTIMIZATIONS)
    if(MSVC)
        foreach(target IN LISTS application_targets)
            # /O2 already enables function-level linking; package global data too.
            target_compile_options(${target} PRIVATE "$<${size_configuration}:/Gw>")
        endforeach()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
        foreach(target IN LISTS application_targets)
            target_compile_options(${target} PRIVATE
                "$<${size_configuration}:-ffunction-sections>"
                "$<${size_configuration}:-fdata-sections>")
        endforeach()
        include(CheckLinkerFlag)
        check_linker_flag(CXX "LINKER:--gc-sections" WXNOTEPAD_LINKER_GC_SECTIONS)
        check_linker_flag(CXX "LINKER:-z,pack-relative-relocs" WXNOTEPAD_LINKER_RELR)
    endif()

    # Native regressions link the same archives as the application. Keep their
    # Release links consistent so tests exercise the same section elimination.
    get_property(size_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS size_targets)
        get_target_property(target_type ${target} TYPE)
        if(NOT target_type STREQUAL "EXECUTABLE")
            continue()
        endif()
        if(MSVC)
            target_link_options(${target} PRIVATE
                "$<${size_configuration}:/OPT:REF,ICF>"
                "$<${size_configuration}:/INCREMENTAL:NO>")
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
            # LTO emits native sections during the final link, including when
            # a regression executable consumes an IPO-enabled static archive.
            target_link_options(${target} PRIVATE
                "$<${size_configuration}:-ffunction-sections>"
                "$<${size_configuration}:-fdata-sections>")
            if(WXNOTEPAD_LINKER_GC_SECTIONS)
                target_link_options(${target} PRIVATE "$<${size_configuration}:LINKER:--gc-sections>")
            endif()
            # The packaged Linux runtime targets Ubuntu 26.04+. The linker probe
            # also links against libc, including its DT_RELR ABI requirement.
            if(WXNOTEPAD_LINKER_RELR)
                target_link_options(${target} PRIVATE "$<${size_configuration}:LINKER:-z,pack-relative-relocs>")
            endif()
        endif()
    endforeach()
endif()

if(WXNOTEPAD_ENABLE_IPO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error LANGUAGES CXX)
    if(NOT ipo_supported)
        message(FATAL_ERROR "IPO is not supported by this toolchain: ${ipo_error}")
    endif()
    foreach(target IN LISTS application_targets)
        set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL TRUE)
    endforeach()
endif()
