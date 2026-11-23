#include "app/app_icon.h"
#ifndef __WXMSW__
#include "app_icon_data.h"
#include <wx/imagbmp.h>
#include <wx/mstream.h>
#endif

wxIconBundle ApplicationIcons() {
#ifdef __WXMSW__
    // windows.rc already embeds every size in the executable's icon group.
    return wxIconBundle("APP_ICON", nullptr);
#else
    if (!wxImage::FindHandler(wxBITMAP_TYPE_ICO)) wxImage::AddHandler(new wxICOHandler);
    wxMemoryInputStream input(AppIconData, sizeof(AppIconData));
    return wxIconBundle(input, wxBITMAP_TYPE_ICO);
#endif
}
