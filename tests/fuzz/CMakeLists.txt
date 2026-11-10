# Compile the actual production implementations without linking the GUI runtime.
add_library(fuzz_subjects STATIC
    ../../src/document/file_variables.cpp
    ../../src/document/encoding/encoding_detection.cpp
    ../../src/document/encoding/utf16_codec.cpp
    ../../src/document/encoding/gbk_codec.cpp
    ../../src/document/encoding/single_byte_codec.cpp
    ../../src/document/encoding/chinese_codec.cpp
    "${NOTEPAD4_SOURCE}/scintilla/src/UniConversion.cxx")
target_include_directories(fuzz_subjects PUBLIC ../../src
    PRIVATE "${NOTEPAD4_SOURCE}/scintilla/src")
target_link_libraries(fuzz_subjects PUBLIC wx::base)
if(MSVC)
    target_compile_options(fuzz_subjects PRIVATE /utf-8 /Zc:char8_t-)
else()
    target_compile_options(fuzz_subjects PRIVATE -fno-char8_t)
endif()

if(WXNOTEPAD_FUZZ_ONLY)
    target_compile_options(fuzz_subjects PUBLIC
        -fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
    target_link_options(fuzz_subjects PUBLIC -fsanitize=address,undefined)
endif()

foreach(subject codecs detection file_variables)
    add_executable(fuzz_${subject}_replay ${subject}.cpp replay.cpp)
    target_link_libraries(fuzz_${subject}_replay PRIVATE fuzz_subjects)
    if(MSVC)
        target_compile_options(fuzz_${subject}_replay PRIVATE /utf-8)
    endif()
    if(BUILD_TESTING)
        add_test(NAME fuzz_${subject}_corpus COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run.py"
            replay --target ${subject} --binary "$<TARGET_FILE:fuzz_${subject}_replay>"
            --output "${CMAKE_BINARY_DIR}/Testing/Temporary/fuzz/${subject}")
        set_tests_properties(fuzz_${subject}_corpus PROPERTIES TIMEOUT 30)
        if(UNIX)
            set_tests_properties(fuzz_${subject}_corpus PROPERTIES ENVIRONMENT "LC_ALL=C.UTF-8")
        endif()
    endif()
    if(WXNOTEPAD_FUZZ_ONLY)
        add_executable(fuzz_${subject} ${subject}.cpp)
        target_link_libraries(fuzz_${subject} PRIVATE fuzz_subjects)
        target_link_options(fuzz_${subject} PRIVATE -fsanitize=fuzzer)
    endif()
endforeach()
