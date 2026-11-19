#pragma once

#include <array>
#include <cstdint>
#include <ScintillaTypes.h>

enum class PrintHeader { FilenameAndDateTime, FilenameAndDate, Filename, Blank };
enum class PrintFooter { PageNumber, Blank };

struct PrintOptions {
    PrintHeader header = PrintHeader::FilenameAndDate;
    PrintFooter footer = PrintFooter::PageNumber;
    Scintilla::PrintOption colour = Scintilla::PrintOption::ColourOnWhite;
    int zoom = 100;
    std::array<int, 4> margins = {-1, -1, -1, -1};
};

inline constexpr std::array PrintMarginKeys = {"PrintMarginLeft", "PrintMarginTop", "PrintMarginRight",
                                               "PrintMarginBottom"};
