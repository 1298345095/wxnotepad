#pragma once

#include <wx/string.h>
class wxWindow;

// Open a local file or URI through its native desktop association.
bool LaunchDesktopTarget(wxWindow& parent, const wxString& target, const wxString& workingDirectory, wxString& error);
#ifdef __WXGTK__
struct _GAppInfo;
bool LaunchDesktopApplication(_GAppInfo* application, const wxString& target, const wxString& workingDirectory,
                              wxString& error);
#endif
