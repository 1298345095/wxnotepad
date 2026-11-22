#pragma once

#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <algorithm>
#include <memory>

// Layout policies mirror Notepad4's dialog templates and ResizeDlg_Init[X/Y2].
// Sizes come from native controls and translated text, not frozen pixel bounds.
namespace DialogLayout {
enum class Resize { Fixed, Horizontal, Both };
inline constexpr int MarginDip = 10;
inline constexpr int GapDip = 5;

inline long Style(Resize resize) {
    return wxDEFAULT_DIALOG_STYLE | (resize == Resize::Fixed ? 0 : wxRESIZE_BORDER);
}

inline void Fit(wxDialog& dialog, wxSizer* contents, Resize resize = Resize::Fixed, bool retainWidth = false) {
    const int width = retainWidth ? dialog.GetSize().x : 0;
    // A Find/Replace switch changes its required height. Release the previous
    // hints before measuring the newly visible controls and lock the new height.
    dialog.SetMinSize(wxDefaultSize);
    dialog.SetMaxSize(wxDefaultSize);
    if (dialog.GetSizer() != contents) dialog.SetSizer(contents);
    contents->SetSizeHints(&dialog);
    const auto minimum = dialog.GetSize();
    dialog.SetMinSize(minimum);
    if (resize == Resize::Horizontal) dialog.SetMaxSize(wxSize(-1, minimum.y));
    if (retainWidth) dialog.SetSize(wxSize(std::max(width, minimum.x), minimum.y));
    dialog.Layout();
}

// Hold an unfinished layout until wx has adopted it. If building its controls
// throws, the local owner releases the unparented sizers rather than leaking them.
template <class Sizer>
void Fit(wxDialog& dialog, std::unique_ptr<Sizer> contents, Resize resize = Resize::Fixed, bool retainWidth = false) {
    dialog.SetSizer(contents.get());
    Fit(dialog, contents.release(), resize, retainWidth);
}

// IDD_ALIGN, IDD_SORT, IDD_LINENUM, IDD_COLUMNWRAP, IDD_LONGLINES,
// IDD_DEFEOLMODE and IDD_CHANGENOTIFY place OK/Cancel beside the fields.
inline std::unique_ptr<wxSizer> WithSideButtons(wxDialog& dialog, wxSizer* fields) {
    auto layout = std::make_unique<wxBoxSizer>(wxHORIZONTAL);
    layout->Add(fields, 1, wxEXPAND | wxBOTTOM, dialog.FromDIP(MarginDip));
    auto buttons = std::make_unique<wxBoxSizer>(wxVERTICAL);
    // Native controls are owned by their parent window from construction.
    auto* ok = new wxButton(&dialog, wxID_OK);
    ok->SetDefault();
    buttons->Add(ok, 0, wxEXPAND | wxBOTTOM, dialog.FromDIP(GapDip));
    buttons->Add(new wxButton(&dialog, wxID_CANCEL), 0, wxEXPAND);
    layout->Add(buttons.release(), 0, wxALL, dialog.FromDIP(MarginDip));
    return layout;
}
} // namespace DialogLayout
