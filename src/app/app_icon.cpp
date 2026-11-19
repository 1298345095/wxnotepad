#include "app/app_icon.h"
#include "app_icon_data.h"
#include <wx/iconbndl.h>
#include <wx/imagbmp.h>
#include <wx/mstream.h>

wxIconBundle ApplicationIcons() {
    if (!wxImage::FindHandler(wxBITMAP_TYPE_ICO)) wxImage::AddHandler(new wxICOHandler);
    wxMemoryInputStream input(AppIconData, sizeof(AppIconData));
    return wxIconBundle(input, wxBITMAP_TYPE_ICO);
}
