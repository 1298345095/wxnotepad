#include "editor_frame.h"
#include "editor_control.h"
#include <wx/wx.h>

class NotepadApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        frame->Editor()->SetFocus();
        return true;
    }
};

wxIMPLEMENT_APP(NotepadApp);
