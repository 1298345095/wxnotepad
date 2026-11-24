#include "diagnostics/print.h"
#include "native_key_helpers.h"
#include "settings/settings_store.h"
#include "file_helpers.h"
#include "app/localization.h"
#include "open_target_windows.h"
#include <wx/stdpaths.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <array>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <stdexcept>
#ifdef __WXMSW__
#include "native_published_file.h"
#endif

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::string PublishedBytes(const std::filesystem::path& path) {
#ifdef __WXMSW__
    return NativePublishedFile::Read(path);
#else
    return TestFiles::Bytes(path);
#endif
}
class ActionFeedback final : public wxModalDialogHook {
public:
    ActionFeedback() { Register(); }
    int Enter(wxDialog* dialog) override {
        titles.push_back(dialog->GetTitle());
        return dialog->GetTitle() == "Could not open target" ? wxID_OK : wxID_CANCEL;
    }
    std::vector<wxString> titles;
};
struct SelectionState {
    intptr_t anchor, caret, anchorVirtualSpace, caretVirtualSpace;
    bool operator==(const SelectionState&) const = default;
};
struct FoldState {
    intptr_t level, expanded, visible;
    bool operator==(const FoldState&) const = default;
};
struct DocumentState {
    std::string text;
    wxString path;
    FileEncoding encoding;
    bool bom;
    intptr_t eol, readonly, modified, undo, redo, selectionMode, mainSelection;
    std::vector<SelectionState> selections;
    std::vector<FoldState> folds;
    bool operator==(const DocumentState&) const = default;
};
DocumentState State(EditorFrame* frame) {
    auto* editor = frame->Editor();
    DocumentState state{TestEditor::Text(frame),
                        frame->DocumentPath(),
                        frame->DocumentEncoding(),
                        frame->DocumentHasBom(),
                        editor->Send(Message::GetEOLMode),
                        editor->Send(Message::GetReadOnly),
                        editor->Send(Message::GetModify),
                        editor->Send(Message::CanUndo),
                        editor->Send(Message::CanRedo),
                        editor->Send(Message::GetSelectionMode),
                        editor->Send(Message::GetMainSelection),
                        {},
                        {}};
    for (intptr_t index = 0; index < editor->Send(Message::GetSelections); ++index) {
        const auto selection = static_cast<uintptr_t>(index);
        state.selections.push_back({editor->Send(Message::GetSelectionNAnchor, selection),
                                    editor->Send(Message::GetSelectionNCaret, selection),
                                    editor->Send(Message::GetSelectionNAnchorVirtualSpace, selection),
                                    editor->Send(Message::GetSelectionNCaretVirtualSpace, selection)});
    }
    for (intptr_t index = 0; index < editor->Send(Message::GetLineCount); ++index) {
        const auto line = static_cast<uintptr_t>(index);
        state.folds.push_back({editor->Send(Message::GetFoldLevel, line), editor->Send(Message::GetFoldExpanded, line),
                               editor->Send(Message::GetLineVisible, line)});
    }
    return state;
}
const wxDataFormat& ClipboardFormat() {
    static const wxDataFormat format("application/x-wxnotepad-selection265-owned");
    return format;
}
const std::string ClipboardBytes("\0\1\x7f\xff", 4);
void SeedClipboard() {
    wxTheClipboard->UsePrimarySelection(false);
    wxClipboardLocker lock;
    Require(!!lock, "open owned clipboard publication");
    auto* data = new wxDataObjectComposite;
    data->Add(new wxTextDataObject(wxString::FromUTF8("selection action clipboard 中文")), true);
    auto* custom = new wxCustomDataObject(ClipboardFormat());
    custom->SetData(ClipboardBytes.size(), ClipboardBytes.data());
    data->Add(custom);
    Require(wxTheClipboard->SetData(data), "publish owned text and opaque clipboard bytes");
}
void CheckClipboard() {
    wxTheClipboard->UsePrimarySelection(false);
    wxClipboardLocker lock;
    Require(!!lock, "open clipboard after native selection action");
    wxTextDataObject text;
    wxCustomDataObject custom(ClipboardFormat());
    Require(wxTheClipboard->GetData(text) && text.GetText() == wxString::FromUTF8("selection action clipboard 中文") &&
                wxTheClipboard->GetData(custom) &&
                std::string(static_cast<const char*>(custom.GetData()), custom.GetSize()) == ClipboardBytes,
            "selection action preserves independent clipboard text and opaque format bytes");
}
void Menus(EditorFrame* frame) {
    constexpr std::array labels{"Search with Google", "Search with Bing", "Search on Wikipedia", "Custom Action 1",
                                "Custom Action 2"};
    for (const auto* label : labels) {
        const int id = frame->GetMenuBar()->FindMenuItem("Tools", label);
        Require(id != wxNOT_FOUND, "Tools Action on Selection exposes all five original commands");
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(!frame->GetMenuBar()->FindItem(id)->IsEnabled(), "empty selection disables selection actions");
    }
    TestEditor::ResetStream(frame->Editor(), "query", 0, 5);
    frame->Editor()->Send(Message::SetReadOnly, true);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    for (const auto* label : labels) {
        const int id = frame->GetMenuBar()->FindMenuItem("Tools", label);
        Require(frame->GetMenuBar()->FindItem(id)->IsEnabled(),
                "selected read-only text enables providers and even unconfigured custom commands");
    }
    frame->Editor()->Send(Message::SetReadOnly, false);
    std::puts("PASS: five original selection actions and original selection/read-only availability");
}
class ActionCases {
public:
    ActionCases(EditorFrame* frame, SettingsStore& settings) : frame_(frame), settings_(settings) {
        const auto directory =
            TestFiles::TemporaryDirectory("selection-action-child") / TestFiles::Utf8Path("program folder 中文");
        std::filesystem::create_directories(directory);
        wxFileName child(wxStandardPaths::Get().GetExecutablePath());
#ifdef __WXMSW__
        child.SetFullName("run_command_child.exe");
        const auto copy = directory / TestFiles::Utf8Path("record program 中文.exe");
        for (const auto& dependency :
             std::filesystem::directory_iterator(TestFiles::Utf8Path(child.GetPath().utf8_string())))
            if (dependency.path().extension() == ".dll")
                std::filesystem::copy_file(dependency.path(), directory / dependency.path().filename());
#else
        child.SetFullName("run_command_child");
        const auto copy = directory / TestFiles::Utf8Path("record program 中文");
#endif
        std::filesystem::copy_file(TestFiles::Utf8Path(child.GetFullPath().utf8_string()), copy);
        command = "\"" + TestFiles::Filename(copy) + "\" \"%s\" \"%%WX265_VALUE%%\" literal=%%%%";
        commands.fill(command);
        wxSetEnv("WX265_PROGRAM", TestFiles::Filename(copy));
        wxSetEnv("WX265_VALUE", wxString::FromUTF8("expanded 中文 value"));
        wxUnsetEnv("WXNOTEPAD_RUN_DOCUMENT");
        expectedDirectory = wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath();
        Configure();
        SeedClipboard();
        CheckClipboard();
    }
    void Configure() {
        constexpr std::array keys{"GoogleSearchUrl", "BingSearchUrl", "WikiSearchUrl", "CustomAction1",
                                  "CustomAction2"};
        wxString text = "; preserve selection action comment\r\n[Settings]\r\nUnrelatedSetting=keep\r\n[sEtTiNgS2]\r\n";
        for (size_t index = 0; index < keys.size(); ++index)
            text += wxString(keys[index]).Lower() + "=" + commands[index] + "\r\n";
        text += wxString::FromUTF8("[Unrelated]\r\nValue=保留\r\n");
        wxFFile ini(settings_.Path(), "wb");
        Require(ini.IsOpened() && ini.Write(wxString(wxUniChar(0xFEFF)) + text, wxMBConvUTF16LE()) && ini.Close(),
                "configure all five original keys in their case-insensitive Settings2 section");
    }
    void Invoke(unsigned action, const std::optional<std::string>& escaped, bool shortcut = false) {
        const auto record = TestFiles::TemporaryDirectory("selection-action-child") /
                            ("received-" + std::to_string(++sequence_) + ".txt");
        lastRecord = record;
        wxSetEnv("WXNOTEPAD_RUN_RECORD", TestFiles::Filename(record));
        constexpr std::array labels{"Search with Google", "Search with Bing", "Search on Wikipedia", "Custom Action 1",
                                    "Custom Action 2"};
        wxYield();
        const auto before = State(frame_);
        frame_->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        if (shortcut)
            NativeKeys::Shortcut(frame_, action == 3 ? '1' : '2', true, false, true);
        else
            NativeSearch::Command(frame_, frame_->GetMenuBar()->FindMenuItem("Tools", labels[action]));
        if (escaped) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!std::filesystem::exists(record) && std::chrono::steady_clock::now() < deadline) {
                wxYield();
                wxMilliSleep(10);
            }
            Require(std::filesystem::exists(record), "selection action launches an independent native recording child");
            const auto expected =
                expectedDirectory.utf8_string() + "\n" + *escaped + "\nexpanded 中文 value\nliteral=%%\n";
            const auto observed = PublishedBytes(record);
            if (observed != expected)
                DiagnosticPrint::Print(stderr, "Original child expected <{}>, observed <{}>\n", expected, observed);
            Require(observed == expected, "independent native child argv/cwd matches pinned original capture");
            DiagnosticPrint::Print(stdout, "PASS: actual child cwd/argv {}\n", observed);
        } else {
            wxMilliSleep(250);
            wxYield();
            Require(!std::filesystem::exists(record),
                    "action without a successful native launch creates no recording child");
        }
        Require(State(frame_) == before,
                "action preserves bytes, encoding/BOM/EOL, selections, folds, save point and history");
        CheckClipboard();
    }
    void Stream(const char* input, const std::optional<std::string>& escaped, unsigned action = 3,
                bool shortcut = false) {
        TestEditor::ResetStream(frame_->Editor(), input, 0, static_cast<intptr_t>(std::strlen(input)));
        Invoke(action, escaped, shortcut);
    }
    wxString command;
    wxString expectedDirectory;
    std::array<wxString, 5> commands;
    std::filesystem::path lastRecord;

private:
    EditorFrame* frame_;
    SettingsStore& settings_;
    unsigned sequence_ = 0;
};
void ExecutableActions(EditorFrame* frame, SettingsStore& settings) {
    ActionFeedback feedback;
    ActionCases cases(frame, settings);
    for (unsigned action = 0; action < 5; ++action)
        cases.Stream(" one two 中 ", "one%20two%20%E4%B8%AD", action);
    cases.Stream(" a:b/c?d=e&f#tail ", "a:b/c?d=e&f#tail");
    cases.Stream(" first\r\nsecond line 中 ", "first%0D%0Asecond%20line%20%E4%B8%AD");
    cases.Stream(" \a\b\f\n\r\t\v A B \a\b\f\n\r\t\v", "A%20B");
    cases.Stream("$WX265_VALUE O'Reilly", "$WX265_VALUE%20O'Reilly");
    cases.Stream("%WX265_VALUE%", "expanded 中文 value");
    cases.commands[3] = cases.command;
    cases.commands[3].Replace("\"%s\"", "%s");
    cases.Configure();
    cases.Stream("O'Reilly", "O'Reilly");
    cases.commands[3] = "\"%%WX265_PROGRAM%%\" \"%s\" \"%%WX265_VALUE%%\" literal=%%%%";
    cases.Configure();
    cases.Stream("one two", "one%20two");
    cases.commands.fill(cases.command);
    cases.Configure();
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "abXX\r\ncdYY");
    editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
    editor->Send(Message::SetRectangularSelectionAnchor, 0);
    editor->Send(Message::SetRectangularSelectionCaret, 7);
    cases.Invoke(3, "a%0D%0Ac");
    TestEditor::ResetStream(editor, "abXX\r\ncdYY");
    editor->Send(Message::SetMultipleSelection, 1);
    editor->Send(Message::SetSelection, 2, 0);
    editor->Send(Message::AddSelection, 8, 6);
    cases.Invoke(3, "ab%0D%0Acd");
    cases.Stream("one two", "one%20two", 3, true);
    cases.Stream("one two", "one%20two", 4, true);
    TestEditor::ResetStream(editor, "one two", 7, 0);
    editor->Send(Message::SetReadOnly, true);
    cases.Invoke(3, "one%20two");
    editor->Send(Message::SetReadOnly, false);
    cases.Stream("", std::nullopt);
    cases.Stream("", std::nullopt, 3, true);
    TestEditor::ResetStream(editor, "word", 1, 1);
    cases.Invoke(3, std::nullopt);
    cases.Stream(" \a\b\f\n\r\t\v", std::nullopt);
    cases.commands[4].clear();
    cases.Configure();
    cases.Stream("query", std::nullopt, 4);
    cases.Stream("query", std::nullopt, 4, true);
    cases.commands[4] = cases.command;
    cases.Configure();
    Require(settings.Save(true), "save ordinary settings while preserving selection command configuration");
    const auto saved = TestFiles::Bytes(TestFiles::Utf8Path(settings.Path().utf8_string()));
    const wxString ini(saved.data(), wxMBConvUTF16LE(), saved.size());
    Require(ini.Contains("; preserve selection action comment") && ini.Contains("UnrelatedSetting=keep") &&
                ini.Contains(wxString::FromUTF8("Value=保留")),
            "settings save preserves unrelated original INI data");
    for (const auto* key : {"googlesearchurl", "bingsearchurl", "wikisearchurl", "customaction1", "customaction2"})
        Require(ini.Contains(wxString(key) + "=" + cases.command),
                "settings save preserves all five original action keys");
    cases.Stream("one two", "one%20two", 4);
    Require(feedback.titles.empty(), "successful and empty/unconfigured selection commands show no error dialog");
    cases.commands[4] =
        "\"" + TestFiles::Filename(TestFiles::TemporaryDirectory() / "missing selection action.exe") + "\" \"%s\"";
    cases.Configure();
    cases.Stream("query", std::nullopt, 4);
    Require(feedback.titles == std::vector<wxString>{"Could not open target"},
            "failed native selection launch uses the existing observable error dialog");
    cases.commands[4] = cases.command;
    cases.Configure();
    cases.Stream("one two", "one%20two", 4);
    struct EncodedDocument {
        const char* name;
        FileEncoding encoding;
        std::string bytes;
    };
    const std::array documents{
        EncodedDocument{"utf8-bom", FileEncoding::Utf8, "\x{ef}\x{bb}\x{bf} one two 中 \r\n"},
        EncodedDocument{"utf16-bom", FileEncoding::Utf16Le,
                        std::string("\x{ff}\x{fe} \0o\0n\0e\0 \0t\0w\0o\0 \0\x{2d}\x{4e} \0\r\0\n\0", 28)},
        EncodedDocument{"gbk", FileEncoding::Gbk, " one two \x{d6}\x{d0} \r\n"}};
    const auto folder = TestFiles::TemporaryDirectory("selection-action-documents");
    std::filesystem::create_directories(folder);
    cases.expectedDirectory = TestFiles::Filename(folder);
    settings.values.saveBeforeRunningTools = true;
    for (const auto& document : documents) {
        const auto path = folder / (std::string(document.name) + ".txt");
        TestFiles::WriteBytes(path, document.bytes);
        editor->Send(Message::SetSavePoint);
        Require(frame->Open(TestFiles::Filename(path), document.encoding).status == OpenStatus::Opened,
                "open independently encoded named selection document");
        const std::string decoded = " one two 中 \r\n";
        Require(TestEditor::Text(frame) == decoded, "named selection fixture decodes its independent bytes");
        editor->Send(Message::BeginUndoAction);
        TestEditor::Append(frame, "dirty");
        editor->Send(Message::EndUndoAction);
        editor->Send(Message::BeginUndoAction);
        TestEditor::Append(frame, "redo");
        editor->Send(Message::EndUndoAction);
        editor->Send(Message::Undo);
        editor->Send(Message::SetSel, decoded.size(), 0);
        wxYield();
        constexpr uintptr_t FoldBase = 0x400;
        constexpr intptr_t FoldHeader = 0x2000;
        editor->Send(Message::SetFoldLevel, 0, FoldBase | FoldHeader);
        editor->Send(Message::SetFoldLevel, 1, FoldBase + 1);
        editor->Send(Message::SetFoldExpanded, 0, false);
        editor->Send(Message::HideLines, 1, 1);
        editor->Send(Message::SetReadOnly, true);
        wxSetEnv("WXNOTEPAD_RUN_DOCUMENT", TestFiles::Filename(path));
        cases.Invoke(3, "one%20two%20%E4%B8%AD");
        auto diskRecord = cases.lastRecord;
        diskRecord += ".disk";
        Require(TestFiles::Bytes(path) == document.bytes && TestFiles::Bytes(diskRecord) == document.bytes,
                "source and actual child retain exact disk encoding/BOM/EOL despite Save Before Tools and dirty text");
        editor->Send(Message::SetReadOnly, false);
        editor->Send(Message::Redo);
        Require(TestEditor::Text(frame) == decoded + "dirtyredo", "selection launch retains the pending Redo entry");
        editor->Send(Message::Undo);
        Require(TestEditor::Text(frame) == decoded + "dirty", "Undo after Redo restores the pre-launch dirty bytes");
        editor->Send(Message::Undo);
        Require(TestEditor::Text(frame) == decoded && !editor->Send(Message::GetModify),
                "Undo reaches the original save point after native selection launch");
        editor->Send(Message::SetSavePoint);
        wxUnsetEnv("WXNOTEPAD_RUN_DOCUMENT");
    }
    settings.values.saveBeforeRunningTools = false;
    Require(feedback.titles == std::vector<wxString>{"Could not open target"},
            "selection actions ignore Save Before Tools and remain usable after a failed launch");
    std::puts("PASS: all five native commands, original shapes/literals, read-only/empty rules, shortcuts and INI "
              "round trip");
}
void ProviderDefaults(EditorFrame* frame, const std::filesystem::path& directory, long language) {
#ifdef __WXMSW__
    // Only run this mode inside an isolated guest whose HTTP/HTTPS recorder was
    // installed by its owner. MAILTO must retain its independently selected app.
    OwnedTargetAssociations::RequireWebHandlers();
#endif
    constexpr std::array labels{"Search with &Google", "Search with &Bing", "Search on &Wikipedia"};
    // Literal expected native targets from the pinned original boundary capture,
    // including non-component URI punctuation and localized Wikipedia defaults.
    const std::array<std::string, 3> expected{
        "https://www.google.com/search?q=A%20%E4%B8%AD%E6%96%87%20%26?#/",
        "https://www.bing.com/search?q=A%20%E4%B8%AD%E6%96%87%20%26?#/",
        language == 1033 ? "https://en.wikipedia.org/wiki/Special:Search?search=A%20%E4%B8%AD%E6%96%87%20%26?#/"
                         : "https://zh.wikipedia.org/wiki/Special:Search?search=A%20%E4%B8%AD%E6%96%87%20%26?#/"};
    struct ProviderCase {
        const char* label;
        std::string input, target;
    };
    std::vector<ProviderCase> requests;
    for (size_t index = 0; index < labels.size(); ++index)
        requests.push_back({labels[index], "  A 中文 &?#/  ", expected[index]});
    if (language == 1033) {
        constexpr size_t LongSelectionLength = 2000;
        constexpr size_t OriginalGoogleQueryLimit = 992;
        requests.push_back({labels[0], std::string(LongSelectionLength, 'a'),
                            "https://www.google.com/search?q=" + std::string(OriginalGoogleQueryLimit, 'a')});
    }
    for (const auto& request : requests) {
        const auto marker = directory / "desktop-received";
        std::filesystem::remove(marker);
        const auto& input = request.input;
        TestEditor::ResetStream(frame->Editor(), input.c_str(), 0, static_cast<intptr_t>(input.size()));
        frame->Editor()->Send(Message::SetReadOnly, true);
        const int id = frame->GetMenuBar()->FindMenuItem(_("&Tools"), wxGetTranslation(request.label));
        Require(id != wxNOT_FOUND, "localized native menu exposes provider command");
        NativeSearch::Command(frame, id);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!std::filesystem::exists(marker) && std::chrono::steady_clock::now() < deadline) {
            wxYield();
            wxMilliSleep(10);
        }
        Require(std::filesystem::exists(marker), "default provider launches an independent native URI recorder");
        const auto actual = PublishedBytes(marker);
        if (actual != request.target + "\n")
            DiagnosticPrint::Print(stderr, "Original provider expected <{}>, observed <{}>\n", request.target, actual);
        Require(actual == request.target + "\n", "default provider target exactly matches independent original");
        Require(TestEditor::Text(frame) == input && frame->Editor()->Send(Message::GetReadOnly) &&
                    !frame->Editor()->Send(Message::GetModify) && !frame->Editor()->Send(Message::CanUndo),
                "provider dispatch preserves read-only document bytes and history");
        TestEditor::RequireSelection(frame->Editor(), 0, static_cast<intptr_t>(input.size()),
                                     "provider dispatch preserves source selection");
        DiagnosticPrint::Print(stdout, "PASS: original provider native target {}\n", request.target);
    }
    frame->Editor()->Send(Message::SetReadOnly, false);
}
class SelectionActionsApp final : public wxApp {
public:
    bool OnInit() override {
        if (argc == 4 && wxString(argv[1]) == "--desktop-target") {
            const auto directory = TestFiles::Utf8Path(wxString(argv[3]).utf8_string());
            const auto target = wxString(argv[2]).utf8_string() + "\n";
            TestFiles::WriteBytes(directory / ("received-" + std::to_string(wxGetProcessId()) + ".txt"), target);
            TestFiles::WriteBytes(directory / "desktop-received.tmp", target);
            std::filesystem::rename(directory / "desktop-received.tmp", directory / "desktop-received");
            std::exit(0);
        }
        const bool defaults = argc == 4 && wxString(argv[1]) == "--provider-defaults";
        long language = 1033;
        std::filesystem::path directory;
        if (defaults) {
            Require(wxString(argv[3]).ToLong(&language) && (language == 1033 || language == 2052 || language == 1028),
                    "provider mode selects a supported release language");
            directory = TestFiles::Utf8Path(wxString(argv[2]).utf8_string());
            wxSetEnv("WXNOTEPAD_OPEN_TARGET_ROOT", wxString(argv[2]));
            AppLocalization::Initialize(static_cast<uint16_t>(language));
        }
        const auto workingDirectory = TestFiles::TemporaryDirectory("unrelated launch cwd");
        std::filesystem::create_directories(workingDirectory);
        originalWorkingDirectory_ = wxGetCwd();
        Require(wxSetWorkingDirectory(TestFiles::Filename(workingDirectory)),
                "start selection fixture outside the executable and document directories");
        settings_ =
            std::make_unique<SettingsStore>(TestFiles::Filename(TestFiles::TemporaryDirectory() / "actions.ini"));
#ifdef __WXMSW__
        if (argc == 2 && wxString(argv[1]) == "--gdi")
            settings_->values.renderingTechnology = std::to_underlying(Scintilla::Technology::Default);
#endif
        auto* frame = new EditorFrame(settings_.get());
#ifdef __WXMSW__
        Require(frame->Editor()->Send(Message::GetTechnology) == settings_->values.renderingTechnology,
                "native selection fixture applies its requested GDI/DirectWrite renderer");
        DiagnosticPrint::Print(stdout, "BEGIN: native selection renderer {}\n",
                               frame->Editor()->Send(Message::GetTechnology));
#endif
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame, defaults, directory, language](bool presented) {
            try {
                Require(presented, "compositor presents owned selection-action frame");
                if (defaults)
                    ProviderDefaults(frame, directory, language);
                else {
                    Menus(frame);
                    ExecutableActions(frame, *settings_);
                }
            } catch (const std::exception& error) {
                DiagnosticPrint::Print(stderr, "FAIL: {}\n", error.what());
                result_ = 1;
            }
            if (!wxSetWorkingDirectory(originalWorkingDirectory_)) {
                DiagnosticPrint::Print(stderr,
                                       "FAIL: restore fixture working directory before temporary-root cleanup\n");
                result_ = 1;
            }
            frame->Editor()->Send(Message::SetSavePoint);
            frame->Destroy();
            ExitMainLoop();
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    std::unique_ptr<SettingsStore> settings_;
    wxString originalWorkingDirectory_;
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(SelectionActionsApp);
