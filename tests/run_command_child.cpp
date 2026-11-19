#include <wx/app.h>
#include <wx/ffile.h>
#include <wx/utils.h>
#include <wx/filefn.h>
#include <string>

class Recorder final : public wxAppConsole {
public:
    bool OnInit() override { return true; }
    int OnRun() override {
        wxString output;
        if (!wxGetEnv("WXNOTEPAD_RUN_RECORD", &output)) return 2;
        wxFFile file(output + ".pending", "wb");
        if (!file.IsOpened()) return 3;
        file.Write(wxGetCwd() + "\n");
        for (int index = 1; index < argc; ++index)
            file.Write(wxString(argv[index]) + "\n");
        wxString document;
        if (wxGetEnv("WXNOTEPAD_RUN_DOCUMENT", &document) && !document.empty()) {
            wxFFile input(document, "rb");
            if (!input.IsOpened() || input.Length() < 0) return 4;
            std::string bytes(static_cast<size_t>(input.Length()), '\0');
            if (input.Read(bytes.data(), bytes.size()) != bytes.size()) return 5;
            wxFFile disk(output + ".disk", "wb");
            if (!disk.IsOpened() || disk.Write(bytes.data(), bytes.size()) != bytes.size() || !disk.Close()) return 6;
        }
        // Publish only after the independent observations have reached disk.
        return file.Close() && wxRenameFile(output + ".pending", output, true) ? 0 : 7;
    }
};
wxIMPLEMENT_APP_CONSOLE(Recorder);
