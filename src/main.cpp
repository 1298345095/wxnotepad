#include "editor_control.h"
#include <wx/wx.h>

class NotepadApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new wxFrame(nullptr, wxID_ANY, wxString::FromUTF8("wxnotepad — original core (read-only)"),
                                  wxDefaultPosition, wxSize(900, 600));
        new EditorControl(frame);
        frame->Show();
        return true;
    }
};

wxIMPLEMENT_APP(NotepadApp);
