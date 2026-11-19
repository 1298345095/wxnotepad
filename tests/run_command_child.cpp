#include <wx/app.h>
#include <wx/ffile.h>
#include <wx/utils.h>

class Recorder final : public wxAppConsole {
public:
    bool OnInit() override { return true; }
    int OnRun() override {
        wxString output;
        if (!wxGetEnv("WXNOTEPAD_RUN_RECORD", &output)) return 2;
        wxFFile file(output, "wb");
        if (!file.IsOpened()) return 3;
        file.Write(wxGetCwd() + "\n");
        for (int index = 1; index < argc; ++index)
            file.Write(wxString(argv[index]) + "\n");
        return 0;
    }
};
wxIMPLEMENT_APP_CONSOLE(Recorder);
