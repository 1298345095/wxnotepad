#include "app/dialogs/document_dialogs.h"
#include "app/dialogs/dialog_layout.h"
#include <utility>
#include "app/localization.h"
#include <wx/translation.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/filename.h>
#include <wx/listbox.h>
#include <wx/radiobox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/treectrl.h>
#include <array>
#include <vector>

namespace {
constexpr int DialogPaddingDip = 10;
struct SchemeFilters {
    wxString wildcard;
    std::vector<SaveFileType> types;
};
SchemeFilters FileSchemeFilters(SyntaxLanguage current, const wxString& path, const wxString& favorites, bool open,
                                const SyntaxExtensions& extensions) {
    SchemeFilters result;
    const auto all = [&] {
        const wxString pattern = wxASCII_STR(wxFileSelectorDefaultWildcardStr);
        // TRANSLATORS: Original IDS_FILTER_ALL caption; %s is the platform all-file wildcard (*.* on Windows, * on
        // Unix).
        result.wildcard += wxString::Format(_("All Files (%s)"), pattern) + "|" + pattern + "|";
        result.types.emplace_back(SaveType::AllFiles);
    };
    if (open) all();
    const auto currentBase = SyntaxSettingsLanguage(current);
    const auto add = [&](SyntaxLanguage language, bool active) {
        const auto& definition = SyntaxFor(language);
        wxArrayString patterns;
        for (const auto& extension : SyntaxExtensionList(language, extensions))
            patterns.Add("*." + extension);
        if (const auto extension = wxFileName(path).GetExt();
            active && !extension.empty() && patterns.Index("*." + extension, false) == wxNOT_FOUND)
            patterns.Add("*." + extension);
        const auto pattern = wxJoin(patterns, ';');
        const auto* label = language == SyntaxLanguage::MarkdownGitHub ? wxTRANSLATE("Markdown") : definition.name;
        result.wildcard += wxGetTranslation(label) + " (" + pattern + ")|" + pattern + "|";
        result.types.emplace_back(active ? current : language);
    };
    add(currentBase, true);
    if (currentBase != SyntaxLanguage::PlainText) add(SyntaxLanguage::PlainText, false);
    for (const auto language : FavoriteSyntaxSchemes(favorites)) {
        if (language != currentBase) add(language, false);
    }
    if (!open) all();
    result.wildcard.RemoveLast();
    return result;
}
} // namespace

OpenFileDialog::OpenFileDialog(wxWindow* parent, SyntaxLanguage current, const wxString& path,
                               const wxString& favorites, const SyntaxExtensions& extensions)
    : wxFileDialog(parent, _("Open file"), {}, {},
                   FileSchemeFilters(current, path, favorites, true, extensions).wildcard,
                   wxFD_OPEN | wxFD_FILE_MUST_EXIST) {
}

std::optional<wxString> OpenFileDialog::Choose() {
    if (ShowModal() != wxID_OK) return std::nullopt;
    return GetPath();
}

std::optional<ChosenSaveFile> ChooseSaveFile(wxWindow* parent, const wxString& title, SyntaxLanguage currentLanguage,
                                             const wxString& path, const SaveFileLocation& initial,
                                             const wxString& favorites, const SyntaxExtensions& extensions) {
    // Pinned Styles.cpp::Style_GetFileDialogFilter; unavailable fixed text schemes
    // are omitted. Current dialect remains distinct from its shared base filter.
    const auto filters = FileSchemeFilters(currentLanguage, path, favorites, false, extensions);
    const wxFileName current(path);
    wxFileDialog dialog(parent, title, initial.directory.empty() ? current.GetPath() : initial.directory,
                        initial.filename.empty() ? current.GetFullName() : initial.filename, filters.wildcard,
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    dialog.SetFilterIndex(0);
    if (dialog.ShowModal() != wxID_OK) return std::nullopt;
    return ChosenSaveFile{dialog.GetPath(), filters.types.at(static_cast<size_t>(dialog.GetFilterIndex()))};
}

std::optional<FileEncoding> ChooseReloadEncoding(wxWindow* parent, std::optional<FileEncoding> initial) {
    using enum FileEncoding;
    constexpr std::array choices = {Utf8, Utf16Le, Gbk, Utf16Be, Gb18030, Big5, Windows1252, Dos437};
    wxDialog dialog(parent, wxID_ANY, _("Reload With Encoding"), wxDefaultPosition, wxDefaultSize,
                    DialogLayout::Style(DialogLayout::Resize::Both));
    auto* contents = new wxBoxSizer(wxVERTICAL);
    contents->Add(new wxStaticText(&dialog, wxID_ANY, _("Select source &encoding to reload file:")), 0, wxALL,
                  dialog.FromDIP(DialogPaddingDip));
    auto* source = new wxListBox(&dialog, wxID_ANY);
    source->SetName("Source encoding");
    AppLocalization::SetAccessibleName(*source, _("Source encoding"));
    for (const auto encoding : choices) {
        const auto index = source->Append(FindFileEncodingInfo(encoding)->displayName);
        if (initial == encoding) source->SetSelection(static_cast<int>(index));
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
    DialogLayout::Fit(dialog, contents, DialogLayout::Resize::Both);
    dialog.CentreOnParent();
    source->SetFocus();
    if (dialog.ShowModal() != wxID_OK) return std::nullopt;
    return choices[source->GetSelection()];
}

std::optional<EncodingChoice> ChooseCurrentEncoding(wxWindow* parent, std::optional<EncodingChoice> initial,
                                                    bool defaultSelection) {
    // Notepad4 89b8b36 EditEncoding.cpp sEncodingGroupList and resource labels.
    // Retain delivered catalog entries in a native tree instead of HWND item data.
    wxDialog dialog(parent, wxID_ANY, _("Select Encoding"), wxDefaultPosition, wxDefaultSize,
                    DialogLayout::Style(DialogLayout::Resize::Both));
    auto* contents = new wxBoxSizer(wxVERTICAL);
    contents->Add(
        new wxStaticText(&dialog, wxID_ANY,
                         defaultSelection ? _("Select &default encoding:") : _("Select current file &encoding:")),
        0, wxALL, dialog.FromDIP(DialogPaddingDip));
    auto* tree = new wxTreeCtrl(&dialog, wxID_ANY, wxDefaultPosition, dialog.FromDIP(wxSize(360, 260)),
                                wxTR_HIDE_ROOT | wxTR_HAS_BUTTONS | wxTR_SINGLE);
    tree->SetName(defaultSelection ? "Default encoding" : "Current encoding");
    AppLocalization::SetAccessibleName(*tree, defaultSelection ? _("Default encoding") : _("Current encoding"));
    const auto root = tree->AddRoot(_("Encodings"));
    std::vector<std::pair<wxTreeItemId, EncodingChoice>> choices;
    const auto add = [&](wxTreeItemId group, const wxString& label, EncodingChoice choice) {
        const auto item = tree->AppendItem(group, label);
        choices.emplace_back(item, choice);
        if (choice == initial) tree->SelectItem(item);
    };
    if (const auto native = NativeEncodingChoice()) add(root, "ANSI", *native);
    if (const auto oem = NativeOemEncodingChoice()) add(root, "OEM", *oem);
    const auto unicode = tree->AppendItem(root, _("Unicode"));
    add(unicode, _("UTF-16LE with BOM"), EncodingChoice(FileEncoding::Utf16Le, true));
    add(unicode, _("UTF-16BE with BOM"), EncodingChoice(FileEncoding::Utf16Be, true));
    add(unicode, "UTF-16LE", EncodingChoice(FileEncoding::Utf16Le));
    add(unicode, "UTF-16BE", EncodingChoice(FileEncoding::Utf16Be));
    add(unicode, "UTF-8", EncodingChoice(FileEncoding::Utf8));
    add(unicode, _("UTF-8 with BOM"), EncodingChoice(FileEncoding::Utf8, true));
    const auto chinese = tree->AppendItem(root, _("Chinese"));
    add(chinese, _("Simplified Chinese (GBK, GB2312)"), EncodingChoice(FileEncoding::Gbk));
    add(chinese, _("Simplified Chinese (GB18030)"), EncodingChoice(FileEncoding::Gb18030));
    add(chinese, _("Traditional Chinese (Big5, Big5-HKSCS)"), EncodingChoice(FileEncoding::Big5));
    const auto western = tree->AppendItem(root, _("Western European"));
    add(western, _("Latin-1 (Windows-1252)"), EncodingChoice(FileEncoding::Windows1252));
    add(western, _("OEM United States (DOS-437)"), EncodingChoice(FileEncoding::Dos437));
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
    tree->Bind(wxEVT_TREE_SEL_CHANGED, [accept, &selected](wxTreeEvent&) { accept->Enable(selected().has_value()); });
    tree->Bind(wxEVT_TREE_ITEM_ACTIVATED, [&dialog, &selected](wxTreeEvent&) {
        if (selected()) dialog.EndModal(wxID_OK);
    });
    DialogLayout::Fit(dialog, contents, DialogLayout::Resize::Both);
    dialog.CentreOnParent();
    tree->SetFocus();
    if (dialog.ShowModal() != wxID_OK) return std::nullopt;
    return selected();
}

void ChooseDefaultEncoding(wxWindow* parent, EncodingOptions& options) {
    // Adapted from Notepad4 89b8b36 SelectDefEncodingDlgProc and IDD_DEFENCODING.
    // The accepted nested choice commits immediately; only the five flags wait
    // for outer OK. See licenses/Notepad4.txt.
    wxDialog dialog(parent, wxID_ANY, _("Default Encoding"));
    auto* contents = new wxBoxSizer(wxVERTICAL);
    contents->Add(new wxStaticText(&dialog, wxID_ANY, _("&Default encoding:")), 0, wxALL,
                  dialog.FromDIP(DialogPaddingDip));
    const auto label = [&]() {
        const auto choice = DefaultEncodingChoice(options.defaultEncoding);
        if (!choice) return wxString::Format(_("Code page %d (unavailable)"), options.defaultEncoding);
        const auto name = OriginalEncodingIdentifier(*choice);
        return wxString::FromUTF8(name.data(), name.size());
    };
    auto* current = new wxStaticText(&dialog, wxID_ANY, label());
    current->SetName("Default encoding label");
    contents->Add(current, 0, wxLEFT | wxRIGHT | wxBOTTOM, dialog.FromDIP(DialogPaddingDip));
    auto* choose = new wxButton(&dialog, wxID_ANY, _("Click here to &select default encoding."));
    choose->SetName("Choose default encoding");
    contents->Add(choose, 0, wxLEFT | wxRIGHT | wxBOTTOM, dialog.FromDIP(DialogPaddingDip));
    choose->Bind(wxEVT_BUTTON, [&dialog, &options, current, &label](wxCommandEvent&) {
        if (const auto choice = ChooseCurrentEncoding(&dialog, DefaultEncodingChoice(options.defaultEncoding), true)) {
            options.defaultEncoding = EncodingSetting(*choice);
            current->SetLabel(label());
            dialog.Layout();
        }
    });
    const auto add = [&](const char* name, const wxString& text, bool checked) {
        auto* control = new wxCheckBox(&dialog, wxID_ANY, text);
        control->SetName(name);
        control->SetValue(checked);
        contents->Add(control, 0, wxLEFT | wxRIGHT | wxBOTTOM, dialog.FromDIP(DialogPaddingDip));
        return control;
    };
    const auto* skip =
        add("SkipUnicodeDetection", _("Skip statistical &Unicode (UTF-16) detection."), options.skipUnicodeDetection);
    const auto* ascii = add("LoadASCIIasUTF8", _("Open 7-bit &ASCII files in UTF-8 mode."), options.loadASCIIasUTF8);
    const auto* ansi =
        add("LoadANSIasUTF8", _("Open ANSI (unknown encoding) files in UTF-&8 mode."), options.loadANSIasUTF8);
    const auto* nfo =
        add("LoadNFOasOEM", _("Open ANSI Art (*.&nfo, *.diz) files in DOS-437 mode."), options.loadNFOasOEM);
    const auto* tags = add("NoEncodingTags", _("Don't parse encoding &tags."), options.noEncodingTags);
    contents->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL,
                  dialog.FromDIP(DialogPaddingDip));
    DialogLayout::Fit(dialog, contents);
    dialog.CentreOnParent();
    if (dialog.ShowModal() != wxID_OK) return;
    options.skipUnicodeDetection = skip->GetValue();
    options.loadASCIIasUTF8 = ascii->GetValue();
    options.loadANSIasUTF8 = ansi->GetValue();
    options.loadNFOasOEM = nfo->GetValue();
    options.noEncodingTags = tags->GetValue();
}

std::optional<FileWatchingOptions> ChooseFileWatchingOptions(wxWindow* parent, const FileWatchingOptions& initial) {
    wxDialog dialog(parent, wxID_ANY, _("File Change Notification"));
    auto* layout = new wxBoxSizer(wxVERTICAL);
    const std::array choices = {_("&None."), _("&Show message."), _("&Auto-reload (unmodified).")};
    auto* mode = new wxRadioBox(&dialog, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                static_cast<int>(choices.size()), choices.data(), 1, wxRA_SPECIFY_COLS);
    mode->SetSelection(std::to_underlying(initial.mode));
    layout->Add(mode, 1, wxEXPAND | wxALL, dialog.FromDIP(DialogPaddingDip));
    auto* log = new wxCheckBox(&dialog, wxID_ANY, _("&Monitor continuously updated file (e.g. log file)."));
    auto* keepEnd = new wxCheckBox(&dialog, wxID_ANY, _("&Keep at document end after reloading."));
    auto* reset = new wxCheckBox(&dialog, wxID_ANY, _("&Reset if a new file is opened."));
    log->SetValue(initial.flags & FileWatchingOption_LogFile);
    keepEnd->SetValue(initial.flags & FileWatchingOption_KeepAtEnd);
    reset->SetValue(initial.resetOnNewFile);
    for (auto* checkbox : {log, keepEnd, reset})
        layout->Add(checkbox, 0, wxLEFT | wxRIGHT | wxBOTTOM, dialog.FromDIP(DialogPaddingDip));
    DialogLayout::Fit(dialog, DialogLayout::WithSideButtons(dialog, layout));
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
    : wxDialog(parent, wxID_ANY, _("Line Endings")) {
    // Original Dialogs.cpp::SelectDefLineEndingDlgProc and IDD_DEFEOLMODE.
    auto* contents = new wxBoxSizer(wxVERTICAL);
    contents->Add(new wxStaticText(this, wxID_ANY, _("&Default line ending mode:")), 0, wxALL,
                  FromDIP(DialogPaddingDip));
    mode_ = new wxChoice(this, wxID_ANY);
    mode_->SetName("Default line ending mode");
    AppLocalization::SetAccessibleName(*mode_, _("Default line ending mode"));
    mode_->Append(_("Windows (CR+LF)"));
    mode_->Append(_("Unix/macOS (LF)"));
    mode_->Append(_("Classic Mac OS (CR)"));
    mode_->SetSelection(options.defaultMode);
    contents->Add(mode_, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(DialogPaddingDip));
    const auto option = [&](const wxString& label, bool value) {
        auto* check = new wxCheckBox(this, wxID_ANY, label);
        check->SetValue(value);
        contents->Add(check, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(DialogPaddingDip));
        return check;
    };
    warn_ = option(_("Show inconsistent line endings warning on loading."), options.warn);
    fix_ = option(_("&Ensure consistent line endings when saving."), options.cleanup.fixLineEndings);
    strip_ = option(_("&Strip trailing blanks when saving."), options.cleanup.fixTrailingBlanks);
    DialogLayout::Fit(*this, DialogLayout::WithSideButtons(*this, contents));
    CentreOnParent();
}

std::optional<LineEndingOptions> LineEndingOptionsDialog::Choose() {
    if (ShowModal() != wxID_OK) return std::nullopt;
    return LineEndingOptions{mode_->GetSelection(), warn_->GetValue(), {fix_->GetValue(), strip_->GetValue()}};
}

LineEndingConversionDialog::LineEndingConversionDialog(wxWindow* parent, const LineEndingCounts& counts,
                                                       Scintilla::EndOfLine current, bool warn, bool defaultNo)
    : wxDialog(parent, wxID_ANY, _("Inconsistent Line Endings")) {
    auto* contents = new wxBoxSizer(wxVERTICAL);
    contents->Add(new wxStaticText(this, wxID_ANY, _("The line endings in this file are not consistent:")), 0, wxALL,
                  FromDIP(DialogPaddingDip));
    contents->Add(new wxStaticText(this, wxID_ANY,
                                   wxString::Format(wxPLURAL("CR+LF: %llu line.", "CR+LF: %llu lines.", counts.crlf),
                                                    static_cast<unsigned long long>(counts.crlf)) +
                                       "\n" +
                                       wxString::Format(wxPLURAL("LF: %llu line.", "LF: %llu lines.", counts.lf),
                                                        static_cast<unsigned long long>(counts.lf)) +
                                       "\n" +
                                       wxString::Format(wxPLURAL("CR: %llu line.", "CR: %llu lines.", counts.cr),
                                                        static_cast<unsigned long long>(counts.cr))),
                  0, wxLEFT | wxRIGHT, FromDIP(DialogPaddingDip));
    contents->Add(new wxStaticText(this, wxID_ANY, _("&Do you want to normalize the line endings?")), 0, wxALL,
                  FromDIP(DialogPaddingDip));
    const std::array names = {_("Windows (CR+LF)"), _("Unix/macOS (LF)"), _("Classic Mac OS (CR)")};
    mode_ =
        new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, static_cast<int>(names.size()), names.data());
    mode_->SetName("Line ending conversion");
    AppLocalization::SetAccessibleName(*mode_, _("Line ending conversion"));
    mode_->SetSelection(current == Scintilla::EndOfLine::CrLf ? 0 : current == Scintilla::EndOfLine::Lf ? 1 : 2);
    auto choices = std::make_unique<wxBoxSizer>(wxVERTICAL);
    choices->Add(mode_, 0, wxEXPAND);
    always_ = new wxCheckBox(this, wxID_ANY, _("&Always show this dialog."));
    always_->SetValue(warn);
    choices->Add(always_, 0, wxTOP, FromDIP(DialogPaddingDip));
    auto buttons = std::make_unique<wxBoxSizer>(wxVERTICAL);
    auto* yes = new wxButton(this, wxID_OK, _("&Yes"));
    auto* no = new wxButton(this, wxID_CANCEL, _("&No"));
    buttons->Add(yes, 0, wxEXPAND | wxBOTTOM, FromDIP(DialogLayout::GapDip));
    buttons->Add(no, 0, wxEXPAND);
    auto footer = std::make_unique<wxBoxSizer>(wxHORIZONTAL);
    footer->Add(choices.release(), 1, wxRIGHT, FromDIP(DialogPaddingDip));
    footer->Add(buttons.release());
    contents->Add(footer.release(), 0, wxEXPAND | wxALL, FromDIP(DialogPaddingDip));
    (defaultNo ? no : yes)->SetDefault();
    DialogLayout::Fit(*this, contents);
    CentreOnParent();
}

LineEndingConversionChoice LineEndingConversionDialog::Choose() {
    using enum Scintilla::EndOfLine;
    const bool convert = ShowModal() == wxID_OK;
    auto selected = Cr;
    if (mode_->GetSelection() == 0)
        selected = CrLf;
    else if (mode_->GetSelection() == 1)
        selected = Lf;
    return {convert, always_->GetValue(), selected};
}
