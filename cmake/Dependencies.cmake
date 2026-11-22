find_package(wxWidgets 3.3.3 EXACT CONFIG REQUIRED)

set(NOTEPAD4_SOURCE "" CACHE PATH "Pinned, patched source produced by tools/build.py")
if(NOT EXISTS "${NOTEPAD4_SOURCE}/wxnotepad-source.json")
    message(FATAL_ERROR "Use python tools/build.py to prepare the pinned original core first")
endif()

file(READ "${NOTEPAD4_SOURCE}/wxnotepad-source.json" source_record)
string(JSON core_revision GET "${source_record}" revision)
if(NOT core_revision STREQUAL "89b8b36e0ee2c23f73be922315f17261ccfbbe1a")
    message(FATAL_ERROR "Unexpected Notepad4 core revision: ${core_revision}")
endif()
