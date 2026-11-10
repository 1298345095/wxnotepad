#include "app/dialogs/document_dialogs.h"
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/filename.h>
#include <wx/listbox.h>
#include <wx/radiobox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/treectrl.h>
#include <vector>
#include <iterator>

namespace {
constexpr int DialogPaddingDip = 10;
} // namespace

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

std::optional<FileEncoding> ChooseReloadEncoding(wxWindow* parent, std::optional<FileEncoding> initial) {
    struct SourceEncoding {
        const char* label;
        FileEncoding encoding;
    };
    const SourceEncoding choices[] = {{"UTF-8", FileEncoding::Utf8},
                                      {"UTF-16LE", FileEncoding::Utf16Le},
                                      {"GBK (CP936)", FileEncoding::Gbk},
                                      {"GB18030 (CP54936)", FileEncoding::Gb18030},
                                      {"Big5 (CP950)", FileEncoding::Big5}};
    wxDialog dialog(parent, wxID_ANY, "Reload With Encoding");
    auto* contents = new wxBoxSizer(wxVERTICAL);
    contents->Add(new wxStaticText(&dialog, wxID_ANY, "Select source &encoding to reload file:"), 0, wxALL,
                  dialog.FromDIP(DialogPaddingDip));
    auto* source = new wxListBox(&dialog, wxID_ANY);
    source->SetName("Source encoding");
    for (const auto& choice : choices) {
        const auto index = source->Append(choice.label);
        if (initial == choice.encoding) source->SetSelection(static_cast<int>(index));
    }
    contents->Add(source, 1, wxEXPAND | wxLEFT | wxRIGHT, dialog.FromDIP(DialogPaddingDip));
    contents->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL,
                  dialog.FromDIP(DialogPaddingDip));
    auto* accept = dialog.FindWindow(wxID_OK);
    accept->Enable(source->GetSelection() != wxNOT_FOUND);
    dialog.Bind(wxEVT_LISTBOX,
                [source, accept](wxCommandEvent&) { accept->Enable(source->GetSelection() != wxNOT_FOUND); });
    dialog.Bind(wxEVT_LISTBOX_DCLICK, [&dialog, source](wxCommandEvent&) {
        if (source->GetSelection() != wxNOT_FOUND) dialog.EndModal(wxID_OK);
    });
    dialog.SetSizerAndFit(contents);
    dialog.CentreOnParent();
    source->SetFocus();
    if (dialog.ShowModal() != wxID_OK) return std::nullopt;
    return choices[source->GetSelection()].encoding;
}

std::optional<EncodingChoice> ChooseCurrentEncoding(wxWindow* parent, EncodingChoice initial) {
    // Notepad4 89b8b36 EditEncoding.cpp sEncodingGroupList and resource labels.
    // Retain delivered catalog entries in a native tree instead of HWND item data.
    wxDialog dialog(parent, wxID_ANY, "Select Encoding");
    auto* contents = new wxBoxSizer(wxVERTICAL);
    contents->Add(new wxStaticText(&dialog, wxID_ANY, "Select current file &encoding:"), 0, wxALL,
                  dialog.FromDIP(DialogPaddingDip));
    auto* tree = new wxTreeCtrl(&dialog, wxID_ANY, wxDefaultPosition, dialog.FromDIP(wxSize(360, 260)),
                                wxTR_HIDE_ROOT | wxTR_HAS_BUTTONS | wxTR_SINGLE);
    tree->SetName("Current encoding");
    const auto root = tree->AddRoot("Encodings");
    std::vector<std::pair<wxTreeItemId, EncodingChoice>> choices;
    const auto add = [&](wxTreeItemId group, const char* label, EncodingChoice choice) {
        const auto item = tree->AppendItem(group, label);
        choices.emplace_back(item, choice);
        if (choice == initial) tree->SelectItem(item);
    };
    if (const auto native = NativeEncodingChoice()) add(root, "ANSI", *native);
    const auto unicode = tree->AppendItem(root, "Unicode");
    add(unicode, "UTF-16LE with BOM", EncodingChoice(FileEncoding::Utf16Le, true));
    add(unicode, "UTF-16LE", EncodingChoice(FileEncoding::Utf16Le));
    add(unicode, "UTF-8", EncodingChoice(FileEncoding::Utf8));
    add(unicode, "UTF-8 with BOM", EncodingChoice(FileEncoding::Utf8, true));
    const auto chinese = tree->AppendItem(root, "Chinese");
    add(chinese, "Simplified Chinese (GBK, GB2312)", EncodingChoice(FileEncoding::Gbk));
    add(chinese, "Simplified Chinese (GB18030)", EncodingChoice(FileEncoding::Gb18030));
    add(chinese, "Traditional Chinese (Big5, Big5-HKSCS)", EncodingChoice(FileEncoding::Big5));
    tree->ExpandAll();
    contents->Add(tree, 1, wxEXPAND | wxLEFT | wxRIGHT, dialog.FromDIP(DialogPaddingDip));
    contents->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL,
                  dialog.FromDIP(DialogPaddingDip));
    const auto selected = [&]() -> std::optional<EncodingChoice> {
        for (const auto& [item, choice] : choices)
            if (item == tree->GetSelection()) return choice;
        return std::nullopt;
    };
    auto* accept = dialog.FindWindow(wxID_OK);
    accept->Enable(selected().has_value());
    tree->Bind(wxEVT_TREE_SEL_CHANGED, [&](wxTreeEvent&) { accept->Enable(selected().has_value()); });
    tree->Bind(wxEVT_TREE_ITEM_ACTIVATED, [&](wxTreeEvent&) {
        if (selected()) dialog.EndModal(wxID_OK);
    });
    dialog.SetSizerAndFit(contents);
    dialog.CentreOnParent();
    tree->SetFocus();
    if (dialog.ShowModal() != wxID_OK) return std::nullopt;
    return selected();
}

std::optional<FileWatchingOptions> ChooseFileWatchingOptions(wxWindow* parent, const FileWatchingOptions& initial) {
    wxDialog dialog(parent, wxID_ANY, "File Change Notification");
    auto* layout = new wxBoxSizer(wxVERTICAL);
    const wxString choices[] = {"&None.", "&Show message.", "&Auto-reload (unmodified)."};
    auto* mode = new wxRadioBox(&dialog, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                static_cast<int>(std::size(choices)), choices, 1, wxRA_SPECIFY_COLS);
    mode->SetSelection(static_cast<int>(initial.mode));
    layout->Add(mode, 1, wxEXPAND | wxALL, dialog.FromDIP(DialogPaddingDip));
    auto* log = new wxCheckBox(&dialog, wxID_ANY, "&Monitor continuously updated file (e.g. log file).");
    auto* keepEnd = new wxCheckBox(&dialog, wxID_ANY, "&Keep at document end after reloading.");
    auto* reset = new wxCheckBox(&dialog, wxID_ANY, "&Reset if a new file is opened.");
    log->SetValue(initial.flags & FileWatchingOption_LogFile);
    keepEnd->SetValue(initial.flags & FileWatchingOption_KeepAtEnd);
    reset->SetValue(initial.resetOnNewFile);
    for (auto* checkbox : {log, keepEnd, reset})
        layout->Add(checkbox, 0, wxLEFT | wxRIGHT | wxBOTTOM, dialog.FromDIP(DialogPaddingDip));
    layout->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL,
                dialog.FromDIP(DialogPaddingDip));
    dialog.SetSizerAndFit(layout);
    dialog.CentreOnParent();
    if (dialog.ShowModal() != wxID_OK) return std::nullopt;
    auto options = initial;
    options.mode = static_cast<FileWatchingMode>(mode->GetSelection());
    options.flags =
        (log->GetValue() ? FileWatchingOption_LogFile : 0) | (keepEnd->GetValue() ? FileWatchingOption_KeepAtEnd : 0);
    options.resetOnNewFile = reset->GetValue();
    return options;
}

LineEndingOptionsDialog::LineEndingOptionsDialog(wxWindow* parent, const LineEndingOptions& options)
    : wxDialog(parent, wxID_ANY, "Line Endings") {
    // Original Dialogs.cpp::SelectDefLineEndingDlgProc and IDD_DEFEOLMODE.
    auto* contents = new wxBoxSizer(wxVERTICAL);
    contents->Add(new wxStaticText(this, wxID_ANY, "&Default line ending mode:"), 0, wxALL, FromDIP(DialogPaddingDip));
    mode_ = new wxChoice(this, wxID_ANY);
    mode_->SetName("Default line ending mode");
    mode_->Append("Windows (CR+LF)");
    mode_->Append("Unix/macOS (LF)");
    mode_->Append("Classic Mac OS (CR)");
    mode_->SetSelection(options.defaultMode);
    contents->Add(mode_, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(DialogPaddingDip));
    const auto option = [&](const char* label, bool value) {
        auto* check = new wxCheckBox(this, wxID_ANY, label);
        check->SetValue(value);
        contents->Add(check, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(DialogPaddingDip));
        return check;
    };
    warn_ = option("Show inconsistent line endings warning on loading.", options.warn);
    fix_ = option("&Ensure consistent line endings when saving.", options.cleanup.fixLineEndings);
    strip_ = option("&Strip trailing blanks when saving.", options.cleanup.fixTrailingBlanks);
    contents->Add(CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, FromDIP(DialogPaddingDip));
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
                  FromDIP(DialogPaddingDip));
    contents->Add(new wxStaticText(this, wxID_ANY,
                                   wxString::Format("CR+LF: %llu line(s).\nLF: %llu line(s).\nCR: %llu line(s).",
                                                    static_cast<unsigned long long>(counts.crlf),
                                                    static_cast<unsigned long long>(counts.lf),
                                                    static_cast<unsigned long long>(counts.cr))),
                  0, wxLEFT | wxRIGHT, FromDIP(DialogPaddingDip));
    contents->Add(new wxStaticText(this, wxID_ANY, "&Do you want to normalize the line endings?"), 0, wxALL,
                  FromDIP(DialogPaddingDip));
    const wxString names[] = {"Windows (CR+LF)", "Unix/macOS (LF)", "Classic Mac OS (CR)"};
    mode_ = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, static_cast<int>(std::size(names)), names);
    mode_->SetName("Line ending conversion");
    mode_->SetSelection(current == Scintilla::EndOfLine::CrLf ? 0 : current == Scintilla::EndOfLine::Lf ? 1 : 2);
    contents->Add(mode_, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(DialogPaddingDip));
    always_ = new wxCheckBox(this, wxID_ANY, "&Always show this dialog.");
    always_->SetValue(warn);
    contents->Add(always_, 0, wxALL, FromDIP(DialogPaddingDip));
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    auto* yes = new wxButton(this, wxID_OK, "&Yes");
    auto* no = new wxButton(this, wxID_CANCEL, "&No");
    buttons->Add(yes, 0, wxRIGHT, FromDIP(5));
    buttons->Add(no);
    contents->Add(buttons, 0, wxALIGN_RIGHT | wxALL, FromDIP(DialogPaddingDip));
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
