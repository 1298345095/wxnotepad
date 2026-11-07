#include "app/dialogs/document_dialogs.h"
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/filename.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <vector>

OpenFileDialog::OpenFileDialog(wxWindow* parent)
    : wxFileDialog(parent, "Open file", {}, {}, "Text files (*.txt)|*.txt|All files (*.*)|*.*",
                   wxFD_OPEN | wxFD_FILE_MUST_EXIST) {
}

std::optional<wxString> OpenFileDialog::Choose() {
    if (ShowModal() != wxID_OK) return std::nullopt;
    return GetPath();
}

std::optional<ChosenSaveFile> ChooseSaveFile(wxWindow* parent, const wxString& title, SyntaxLanguage currentLanguage,
                                             const wxString& path, const SaveFileLocation& initial) {
    wxString wildcard;
    std::vector<SaveFileType> types;
    const auto addLanguage = [&](const SyntaxDefinition& definition, bool current) {
        wxArrayString patterns;
        for (const auto* extension : definition.extensions)
            patterns.Add("*." + wxString(extension));
        const auto extension = wxFileName(path).GetExt();
        if (current && !extension.empty() && patterns.Index("*." + extension, false) == wxNOT_FOUND)
            patterns.Add("*." + extension);
        const auto pattern = wxJoin(patterns, ';');
        wildcard += wxString(definition.name) + " (" + pattern + ")|" + pattern + "|";
        types.emplace_back(definition.language);
    };
    addLanguage(SyntaxFor(currentLanguage), true);
    for (const auto& definition : SyntaxCatalog()) {
        if (definition.language != currentLanguage) addLanguage(definition, false);
    }
    wildcard += "All Files (*)|*";
    types.emplace_back(SaveType::AllFiles);
    const wxFileName current(path);
    wxFileDialog dialog(parent, title, initial.directory.empty() ? current.GetPath() : initial.directory,
                        initial.filename.empty() ? current.GetFullName() : initial.filename, wildcard,
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    dialog.SetFilterIndex(0);
    if (dialog.ShowModal() != wxID_OK) return std::nullopt;
    return ChosenSaveFile{dialog.GetPath(), types.at(static_cast<size_t>(dialog.GetFilterIndex()))};
}

LineEndingOptionsDialog::LineEndingOptionsDialog(wxWindow* parent, const LineEndingOptions& options)
    : wxDialog(parent, wxID_ANY, "Line Endings") {
    // Original Dialogs.cpp::SelectDefLineEndingDlgProc and IDD_DEFEOLMODE.
    auto* contents = new wxBoxSizer(wxVERTICAL);
    contents->Add(new wxStaticText(this, wxID_ANY, "&Default line ending mode:"), 0, wxALL, FromDIP(10));
    mode_ = new wxChoice(this, wxID_ANY);
    mode_->SetName("Default line ending mode");
    mode_->Append("Windows (CR+LF)");
    mode_->Append("Unix/macOS (LF)");
    mode_->Append("Classic Mac OS (CR)");
    mode_->SetSelection(options.defaultMode);
    contents->Add(mode_, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));
    const auto option = [&](const char* label, bool value) {
        auto* check = new wxCheckBox(this, wxID_ANY, label);
        check->SetValue(value);
        contents->Add(check, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(10));
        return check;
    };
    warn_ = option("Show inconsistent line endings warning on loading.", options.warn);
    fix_ = option("&Ensure consistent line endings when saving.", options.cleanup.fixLineEndings);
    strip_ = option("&Strip trailing blanks when saving.", options.cleanup.fixTrailingBlanks);
    contents->Add(CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, FromDIP(10));
    SetSizerAndFit(contents);
    CentreOnParent();
}

std::optional<LineEndingOptions> LineEndingOptionsDialog::Choose() {
    if (ShowModal() != wxID_OK) return std::nullopt;
    return LineEndingOptions{mode_->GetSelection(), warn_->GetValue(), {fix_->GetValue(), strip_->GetValue()}};
}

LineEndingConversionDialog::LineEndingConversionDialog(wxWindow* parent, const LineEndingCounts& counts,
                                                       Scintilla::EndOfLine current, bool warn, bool defaultNo)
    : wxDialog(parent, wxID_ANY, "Inconsistent Line Endings") {
    auto* contents = new wxBoxSizer(wxVERTICAL);
    contents->Add(new wxStaticText(this, wxID_ANY, "The line endings in this file are not consistent:"), 0, wxALL,
                  FromDIP(10));
    contents->Add(new wxStaticText(this, wxID_ANY,
                                   wxString::Format("CR+LF: %llu line(s).\nLF: %llu line(s).\nCR: %llu line(s).",
                                                    static_cast<unsigned long long>(counts.crlf),
                                                    static_cast<unsigned long long>(counts.lf),
                                                    static_cast<unsigned long long>(counts.cr))),
                  0, wxLEFT | wxRIGHT, FromDIP(10));
    contents->Add(new wxStaticText(this, wxID_ANY, "&Do you want to normalize the line endings?"), 0, wxALL,
                  FromDIP(10));
    const wxString names[] = {"Windows (CR+LF)", "Unix/macOS (LF)", "Classic Mac OS (CR)"};
    mode_ = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 3, names);
    mode_->SetName("Line ending conversion");
    mode_->SetSelection(current == Scintilla::EndOfLine::CrLf ? 0 : current == Scintilla::EndOfLine::Lf ? 1 : 2);
    contents->Add(mode_, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));
    always_ = new wxCheckBox(this, wxID_ANY, "&Always show this dialog.");
    always_->SetValue(warn);
    contents->Add(always_, 0, wxALL, FromDIP(10));
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    auto* yes = new wxButton(this, wxID_OK, "&Yes");
    auto* no = new wxButton(this, wxID_CANCEL, "&No");
    buttons->Add(yes, 0, wxRIGHT, FromDIP(5));
    buttons->Add(no);
    contents->Add(buttons, 0, wxALIGN_RIGHT | wxALL, FromDIP(10));
    (defaultNo ? no : yes)->SetDefault();
    SetSizerAndFit(contents);
    CentreOnParent();
}

LineEndingConversionChoice LineEndingConversionDialog::Choose() {
    const bool convert = ShowModal() == wxID_OK;
    const auto selected = mode_->GetSelection() == 0   ? Scintilla::EndOfLine::CrLf
                          : mode_->GetSelection() == 1 ? Scintilla::EndOfLine::Lf
                                                       : Scintilla::EndOfLine::Cr;
    return {convert, always_->GetValue(), selected};
}
