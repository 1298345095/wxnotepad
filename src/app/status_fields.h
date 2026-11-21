#pragma once

// Notepad4 89b8b36 Notepad4.h StatusItem order. The flexible field also owns
// transient command feedback, leaving each document metadata field intact.
namespace StatusField {
enum : int {
    Line,
    Column,
    Character,
    Selection,
    SelectedLines,
    Matches,
    Feedback,
    Scheme,
    Encoding,
    Eol,
    Overtype,
    Zoom,
    DocumentSize,
    Count
};
}
