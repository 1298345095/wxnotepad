#pragma once

#include "document/document_session.h"
#include <wx/dialog.h>
#include <wx/filedlg.h>

class wxCheckBox;
class wxChoice;

struct ChosenSaveFile {
    wxString path;
    SaveFileType type;
};

class OpenFileDialog final : public wxFileDialog {
public:
    explicit OpenFileDialog(wxWindow* parent);
    std::optional<wxString> Choose();
};

std::optional<ChosenSaveFile> ChooseSaveFile(wxWindow* parent, const wxString& title, SyntaxLanguage currentLanguage,
                                             const wxString& path);

struct LineEndingOptions {
    int defaultMode;
    bool warn;
    SaveCleanupOptions cleanup;
};

class LineEndingOptionsDialog final : public wxDialog {
public:
    LineEndingOptionsDialog(wxWindow* parent, const LineEndingOptions& options);
    std::optional<LineEndingOptions> Choose();

private:
    wxChoice* mode_;
    wxCheckBox* warn_;
    wxCheckBox* fix_;
    wxCheckBox* strip_;
};

struct LineEndingConversionChoice {
    bool convert;
    bool warn;
    Scintilla::EndOfLine mode;
};

class LineEndingConversionDialog final : public wxDialog {
public:
    LineEndingConversionDialog(wxWindow* parent, const LineEndingCounts& counts, Scintilla::EndOfLine current,
                               bool warn, bool defaultNo);
    LineEndingConversionChoice Choose();

private:
    wxChoice* mode_;
    wxCheckBox* always_;
};
