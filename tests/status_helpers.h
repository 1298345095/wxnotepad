#pragma once
#include "app/status_fields.h"
#include <wx/frame.h>
#include <wx/statusbr.h>

// Preserve existing metadata assertions against the separate native fields.
inline wxString StatusMetadata(wxFrame* frame) {
    auto* status = frame->GetStatusBar();
    wxString eol = status->GetStatusText(StatusField::Eol);
    eol.Replace("+", "");
    return status->GetStatusText(StatusField::Encoding) + " | " + status->GetStatusText(StatusField::Scheme) +
           " | EOL: " + eol;
}
inline wxString StatusPosition(wxFrame* frame) {
    auto* status = frame->GetStatusBar();
    const auto value = [status](int field) {
        return status->GetStatusText(field).BeforeFirst('/').AfterFirst(' ').Strip(wxString::both);
    };
    wxString position = "Ln " + value(StatusField::Line) + ", Col " + value(StatusField::Column);
    const auto selection = value(StatusField::Selection);
    if (selection != "0" && selection != "--") position += " | Sel " + selection;
    return position;
}
