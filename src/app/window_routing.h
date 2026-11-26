#pragma once

#include <wx/string.h>
#include <memory>
#include "document/document_session.h"
#include "editor/go_to_location.h"

class EditorFrame;

struct WindowRequest {
    wxString startup;
    std::optional<EncodingChoice> encoding;
    std::optional<SyntaxLanguage> scheme;
    DocumentLoad::SourceDecoding source;
    std::optional<InitialLocation> initialLocation;
    bool readOnly = false;
    bool ordinaryFile = false;
    Scintilla::Position line = 0;
    Scintilla::Position column = 0;
    wxString startupId;
};

// One receiver per editor process, scoped to its user's native desktop session.
class WindowRouting final {
public:
    enum class Result { NewWindow, Handled, Failed };
    static Result Route(const WindowRequest& request, bool reuseWindow, bool singleFileInstance);
    static void SetStartupId(const wxString& id);
    static wxString StartupId(const wxString& desktopId, const wxString& waylandToken);
    explicit WindowRouting(EditorFrame& frame);
    ~WindowRouting();
    bool IsOk() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
