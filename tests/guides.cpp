#include "app/editor_frame.h"
#include "editor_test_helpers.h"
#include "native_search_helpers.h"
#include "native_key_helpers.h"
#include "file_helpers.h"
#include "settings/settings_store.h"
#include "encoded_file_helpers.h"
#include <wx/app.h>
#include <wx/menu.h>
#include <wx/dialog.h>
#include <wx/textctrl.h>
#include <wx/radiobut.h>
#include <wx/modalhook.h>
#include <wx/timer.h>
#include <wx/settings.h>
#include <functional>
#include <memory>
#include <vector>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Command(EditorFrame* frame, const char* menu, const char* label) {
    const int id = frame->GetMenuBar()->FindMenuItem(menu, label);
    const std::string path = std::string("original command is reachable at ") + menu + " > " + label;
    Require(id != wxNOT_FOUND, path.c_str());
    NativeSearch::Command(frame, id);
}
void Guides(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto fontSize =
        editor->Send(Message::StyleGetSizeFractional, static_cast<uintptr_t>(Scintilla::StylesCommon::Default));
    Require(editor->Send(Message::GetIndentationGuides) == 0, "guides default to hidden");
    Command(frame, "View", "Indentation Guides");
    Require(editor->Send(Message::GetIndentationGuides) == 2, "plain text enables original look-forward guides");
    for (const auto& [language, mode] :
         {std::pair{"C/C++", 3}, std::pair{"Python", 2}, std::pair{"JSON", 3}, std::pair{"Plain Text", 2}}) {
        const int id = frame->GetMenuBar()->FindMenuItem("Scheme", language);
        Require(id != wxNOT_FOUND, "delivered language remains reachable");
        NativeSearch::Command(frame, id);
        Require(editor->Send(Message::GetIndentationGuides) == mode,
                "visible guides follow original delivered-language metadata");
        Require(editor->Send(Message::StyleGetFore, 37) == 0x0080FF &&
                    editor->Send(Message::StyleGetBack, 37) == 0xFFFFFF,
                "scheme installation applies original guide default foreground and inherited background");
    }
    Require(editor->Send(Message::StyleGetSizeFractional, static_cast<uintptr_t>(Scintilla::StylesCommon::Default)) ==
                    fontSize &&
                editor->GetDPI().x > 0,
            "guide styles preserve native base font and current DPI");
    Command(frame, "View", "Indentation Guides");
    Require(editor->Send(Message::GetIndentationGuides) == 0, "second native guide command hides them");
    NativeKeys::Shortcut(frame, 'G', true, false, true);
    Require(editor->Send(Message::GetIndentationGuides) == 2, "native Ctrl+Shift+G enables guides exactly once");
    NativeKeys::Shortcut(frame, 'G', true, false, true);
    Require(editor->Send(Message::GetIndentationGuides) == 0, "native Ctrl+Shift+G hides guides exactly once");
}
void Marker(EditorFrame* frame) {
    auto* editor = frame->Editor();
    Require(editor->Send(Message::GetEdgeMode) == 0, "long-line marker defaults to hidden");
    Require(editor->Send(Message::GetEdgeColumn) == 80, "default shared long-line boundary is 80");
    Command(frame, "View", "Long Line Marker");
    Require(editor->Send(Message::GetEdgeMode) == 1 && editor->Send(Message::GetEdgeColour) == 0x00C0FF,
            "long-line command enables original line mode and default yellow color");
    Command(frame, "View", "Long Line Marker");
    Require(editor->Send(Message::GetEdgeMode) == 0 && editor->Send(Message::GetEdgeColumn) == 80,
            "hiding marker retains its boundary");
    Require(editor->Send(Message::GetEdgeColour) == 0x00C0FF, "hiding a line marker retains its original color");
    NativeKeys::Shortcut(frame, 'L', true, false, true);
    Require(editor->Send(Message::GetEdgeMode) == 1, "native Ctrl+Shift+L enables the marker exactly once");
    NativeKeys::Shortcut(frame, 'L', true, false, true);
    Require(editor->Send(Message::GetEdgeMode) == 0, "native Ctrl+Shift+L hides the marker exactly once");
}
class LimitDriver final : public wxModalDialogHook, public wxEvtHandler {
public:
    LimitDriver(wxString title, std::function<void(wxDialog*)> action)
        : title_(std::move(title)), action_(std::move(action)), timeout_(this) {
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
            error = "owned limit dialog deadline expired";
            if (active_) NativeSearch::Click(active_, wxID_CANCEL);
        });
        Register();
    }
    int Enter(wxDialog* dialog) override {
        if (dialog->GetTitle() != title_) return wxID_NONE;
        active_ = dialog;
        timeout_.StartOnce(5000);
        wxTheApp->CallAfter([this, dialog] {
            NativeKeys::AfterPresentation(dialog, [this, dialog](bool ready) {
                try {
                    Require(ready, "native limit dialog is presented before navigation");
                    seen = true;
                    action_(dialog);
                } catch (const std::exception& failure) {
                    error = failure.what();
                    if (dialog->IsShown()) NativeSearch::Click(dialog, wxID_CANCEL);
                }
            });
        });
        return wxID_NONE;
    }
    void Exit(wxDialog* dialog) override {
        if (dialog == active_) {
            timeout_.Stop();
            active_ = nullptr;
        }
    }
    bool seen = false;
    std::string error;

private:
    wxString title_;
    std::function<void(wxDialog*)> action_;
    wxTimer timeout_;
    wxDialog* active_ = nullptr;
};
void LongLineDialog(EditorFrame* frame, const wxString& expected, const wxString& chosen, int mode = 1,
                    bool accept = true, bool valid = true) {
    LimitDriver driver("Long Lines Settings", [&expected, &chosen, mode, accept, valid](wxDialog* dialog) {
        auto* limit = wxDynamicCast(wxWindow::FindWindowByName("Long line limit", dialog), wxTextCtrl);
        auto* line = wxDynamicCast(wxWindow::FindWindowByName("Edge line", dialog), wxRadioButton);
        auto* background = wxDynamicCast(wxWindow::FindWindowByName("Background color", dialog), wxRadioButton);
        Require(limit && line && background, "native long-line dialog exposes limit and original radio choices");
        Require(limit->GetValue() == expected, "dialog starts at the live current-file limit");
        limit->SetValue(chosen);
        line->SetValue(mode == 1);
        background->SetValue(mode == 2);
        NativeSearch::Click(dialog, accept ? wxID_OK : wxID_CANCEL);
        if (accept && !valid) {
            Require(dialog->IsShown() && limit->HasFocus(), "invalid original numeric input remains open and focused");
            NativeSearch::Click(dialog, wxID_CANCEL);
        } else if (accept)
            Require(!dialog->IsShown(), "valid original numeric input closes the dialog");
    });
    Command(frame, "Settings", "Long Line Settings...");
    Require(driver.error.empty(), driver.error.c_str());
    Require(driver.seen, "original long-line settings opens a native dialog");
}
void WrapSeed(EditorFrame* frame, const wxString& expected) {
    LimitDriver driver("Column Wrap", [&expected](wxDialog* dialog) {
        auto* input = wxDynamicCast(wxWindow::FindWindowByName("Boundary", dialog), wxTextCtrl);
        Require(input != nullptr, "Column Wrap exposes its native boundary control");
        if (input->GetValue() != expected)
            throw std::runtime_error(
                ("Column Wrap boundary: expected " + expected + ", actual " + input->GetValue()).ToStdString());
        NativeSearch::Click(dialog, wxID_CANCEL);
    });
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", "Column Wrap...");
    Require(id != wxNOT_FOUND, "Column Wrap remains reachable");
    NativeSearch::Command(frame, id);
    Require(driver.error.empty(), driver.error.c_str());
    Require(driver.seen, "native Column Wrap exposes original boundary");
}
void DialogKey(wxDialog* dialog, int key, const char* name) {
#ifdef __WXMSW__
    static_cast<void>(name);
    NativeKeys::DialogNavigation(dialog, key);
#else
    const auto wait = [](const auto& condition) {
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!condition() && std::chrono::steady_clock::now() < until) {
            wxYield();
            wxMilliSleep(10);
        }
        Require(condition(), "private native dialog navigation completes");
    };
    wxString path;
    Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &path), "dialog navigation uses private native display");
    const auto request = [&path, &wait](const wxString& command) {
        {
            wxFFile output(path, "wb");
            output.Write(command);
        }
        wait([&path] {
            wxFFile input(path, "rb");
            wxString pending;
            return input.ReadAll(&pending) && pending.empty();
        });
    };
    request("front:Long Lines Settings");
    auto* previous = wxWindow::FindFocus();
    request(wxString("key:") + name);
    wait([dialog, key, previous] {
        return !dialog->IsShown() || (key == WXK_TAB && wxWindow::FindFocus() != previous);
    });
#endif
    wxYield();
}
void DialogNavigation(EditorFrame* frame) {
    const auto boundary = frame->Editor()->Send(Message::GetEdgeColumn);
    for (const int key : {WXK_RETURN, WXK_ESCAPE, WXK_TAB}) {
        LimitDriver driver("Long Lines Settings", [key](wxDialog* dialog) {
            auto* input = wxDynamicCast(wxWindow::FindWindowByName("Long line limit", dialog), wxTextCtrl);
            Require(input != nullptr, "keyboard navigation starts in native limit control");
            input->SetValue("19");
            input->SetFocus();
            if (key == WXK_TAB) {
                DialogKey(dialog, key, "Tab");
                NativeSearch::Click(dialog, wxID_CANCEL);
            } else
                DialogKey(dialog, key, key == WXK_RETURN ? "Return" : "Escape");
        });
        Command(frame, "Settings", "Long Line Settings...");
        Require(driver.seen && driver.error.empty(),
                driver.error.empty() ? "keyboard opens native long-line dialog" : driver.error.c_str());
        Require(frame->Editor()->Send(Message::GetEdgeColumn) == 19,
                "Return accepts; Escape and Tab/Cancel retain accepted boundary");
    }
    LongLineDialog(frame, "19", wxString::Format("%lld", static_cast<long long>(boundary)));
}
void DialogAndModeline(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto path = TestFiles::TemporaryDirectory() / std::filesystem::path(u8"guide-中文.py");
    const std::string before = "# -*- fill-column: 22 -*-\r\nif True:\r\n\tvalue = '中😀'\r\n\r\n    pass\r\n";
    TestFiles::WriteBytes(path, before);
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "open modeline guide fixture");
    Require(editor->Send(Message::GetEdgeColumn) == 22, "fill-column overrides global long-line limit");
    const auto unicode = before.find("中");
    Require(editor->Send(Message::GetColumn, unicode, -1) == 13 &&
                editor->Send(Message::GetColumn, unicode + 3, -1) == 14 &&
                editor->Send(Message::GetColumn, unicode + 7, -1) == 15,
            "Unicode display columns match independent original capture");
    editor->SetIndentationWidths(8, 4);
    Require(editor->Send(Message::GetColumn, unicode, -1) == 17 && editor->Send(Message::GetEdgeColumn) == 22,
            "tab width changes expanded display columns while retaining marker boundary");
    editor->SetIndentationWidths(4, 4);
    editor->Send(Message::SetSel, 40, 8);
    const auto eol = editor->Send(Message::GetEOLMode);
    WrapSeed(frame, "22");
    LongLineDialog(frame, "22", "35", 2);
    Require(editor->Send(Message::GetEdgeColumn) == 35 && editor->Send(Message::GetEdgeMode) == 2,
            "accept updates the current-file boundary and enables background marker");
    const auto light = wxSystemSettings::GetColour(wxSYS_COLOUR_3DLIGHT);
    const auto color = light.Red() | (light.Green() << 8U) | (light.Blue() << 16U);
    Require(editor->Send(Message::GetEdgeColour) == color, "default background uses native system 3D-light fallback");
    WrapSeed(frame, "35");
    Require(frame->Open(std::nullopt).status == OpenStatus::Cancelled && editor->Send(Message::GetEdgeColumn) == 35,
            "cancelled Open retains live boundary");
    Require(frame->Open(TestFiles::Filename(path.parent_path() / "missing-document.py")).status == OpenStatus::Failed &&
                editor->Send(Message::GetEdgeColumn) == 35,
            "failed Open retains live boundary");
    editor->Send(Message::Colourise, 0, -1);
    editor->Send(Message::FoldLine, 1, static_cast<intptr_t>(Scintilla::FoldAction::Contract));
    Require(!editor->Send(Message::GetLineVisible, 2), "nested Python block is folded before display settings");
    LongLineDialog(frame, "35", "18", 1, false);
    Require(!editor->Send(Message::GetLineVisible, 2), "display settings preserve existing folded block");
    Require(editor->Send(Message::GetEdgeMode) == 2 && editor->Send(Message::GetEdgeColumn) == 35,
            "cancel preserves current boundary and remembered background mode");
    Command(frame, "View", "Long Line Marker");
    LongLineDialog(frame, "35", "21", 1, false);
    Require(editor->Send(Message::GetEdgeMode) == 0, "cancel does not enable a hidden marker");
    Command(frame, "View", "Long Line Marker");
    Require(editor->Send(Message::GetEdgeMode) == 2, "showing restores remembered background mode");
    LongLineDialog(frame, "35", "", 1, true, false);
    LongLineDialog(frame, "35", "4097");
    Require(editor->Send(Message::GetEdgeColumn) == 4096, "original limit clamps to 4096");
    LongLineDialog(frame, "4096", "2147483648");
    Require(editor->Send(Message::GetEdgeColumn) == 0, "original UINT-to-int conversion clamps to zero");
    int boundary = 0;
    for (const auto& [input, expected] :
         {std::pair{" ", -1}, std::pair{"-1", -1}, std::pair{" 12", 12}, std::pair{"12 ", -1}, std::pair{"12x", -1},
          std::pair{"１２", 12}, std::pair{"١٢", 12}, std::pair{"4294967295", 0}, std::pair{"4294967296", -1},
          std::pair{"4096", 4096}, std::pair{"2147483647", 4096}, std::pair{"0", 0}}) {
        LongLineDialog(frame, wxString::Format("%d", boundary), wxString::FromUTF8(input), 1, true, expected >= 0);
        if (expected >= 0) boundary = expected;
        Require(editor->Send(Message::GetEdgeColumn) == boundary,
                "original dialog numeric capture matches live edge column");
    }
    Require(TestEditor::Text(editor) == before && TestFiles::Bytes(path) == before,
            "guide and dialog actions preserve text and independent disk bytes");
    TestEditor::RequireSelection(editor, 40, 8, "display settings preserve reverse selection");
    Require(!editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo) && !editor->Send(Message::CanRedo) &&
                editor->Send(Message::GetEOLMode) == eol && frame->DocumentPath() == TestFiles::Filename(path) &&
                frame->DocumentEncoding() == FileEncoding::Utf8,
            "display settings preserve document metadata/history");
    Require(frame->NewDocument() && editor->Send(Message::GetEdgeColumn) == 0,
            "New initializes current boundary from updated global");
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened &&
                editor->Send(Message::GetEdgeColumn) == 22,
            "reopen reparses original fill-column");
    Require(frame->NewDocument(), "create separate Column Wrap recall document");
    TestEditor::ResetStream(editor, "one two", 0, 7);
    {
        LimitDriver driver("Column Wrap", [](wxDialog* dialog) {
            auto* input = wxDynamicCast(wxWindow::FindWindowByName("Boundary", dialog), wxTextCtrl);
            Require(input && input->GetValue() == "0", "New resets Column Wrap recall to current global boundary");
            input->SetValue("7");
            NativeSearch::Click(dialog, wxID_OK);
        });
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Column Wrap..."));
        Require(driver.seen && driver.error.empty(), "accept original Column Wrap recall");
    }
    LongLineDialog(frame, "0", "35");
    WrapSeed(frame, "7");
    editor->Send(Message::SetSavePoint);
    Require(frame->NewDocument(), "New resets accepted Column Wrap recall");
    TestEditor::ResetStream(editor, "one two", 0, 7);
    WrapSeed(frame, "35");
    DialogNavigation(frame);
}
std::vector<intptr_t> SelectionState(EditorControl* editor) {
    std::vector<intptr_t> state;
    for (const auto message :
         {Message::GetModify, Message::CanUndo, Message::CanRedo, Message::GetReadOnly, Message::GetSelectionMode,
          Message::GetMainSelection, Message::GetRectangularSelectionAnchor, Message::GetRectangularSelectionCaret,
          Message::GetRectangularSelectionAnchorVirtualSpace, Message::GetRectangularSelectionCaretVirtualSpace})
        state.push_back(editor->Send(message));
    for (intptr_t index = 0; index < editor->Send(Message::GetSelections); ++index)
        for (const auto message : {Message::GetSelectionNAnchor, Message::GetSelectionNCaret,
                                   Message::GetSelectionNAnchorVirtualSpace, Message::GetSelectionNCaretVirtualSpace})
            state.push_back(editor->Send(message, index));
    return state;
}
void DisplayInvariant(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto text = TestEditor::Text(editor);
    const auto selection = SelectionState(editor);
    NativeKeys::Shortcut(frame, 'G', true, false, true);
    NativeKeys::Shortcut(frame, 'G', true, false, true);
    NativeKeys::Shortcut(frame, 'L', true, false, true);
    NativeKeys::Shortcut(frame, 'L', true, false, true);
    LongLineDialog(frame, wxString::Format("%lld", static_cast<long long>(editor->Send(Message::GetEdgeColumn))), "17",
                   1, false);
    Require(TestEditor::Text(editor) == text && SelectionState(editor) == selection,
            "native display actions retain text, all selection ranges, read-only, modification and Undo/Redo");
}
void SavedHistoryAndViews(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const TestFiles::EncodedFixture fixtures[] = {
        {"utf8", FileEncoding::Utf8, "\xef\xbb\xbf中\r\n", "\xef\xbb\xbf中\r\n!"},
        {"gbk", FileEncoding::Gbk, "\xd6\xd0\r", "\xd6\xd0\r!"},
        {"utf16", FileEncoding::Utf16Le, std::string("\xff\xfe\x2d\x4e\x0a\x00", 6),
         std::string("\xff\xfe\x2d\x4e\x0a\x00\x21\x00", 8)},
    };
    for (const auto& fixture : fixtures) {
        const auto path = TestFiles::TemporaryDirectory() / std::filesystem::path(u8"显示状态-") / fixture.name;
        std::filesystem::create_directories(path.parent_path());
        TestFiles::WriteBytes(path, fixture.before);
        Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                "open encoded display fixture");
        const auto eol = editor->Send(Message::GetEOLMode);
        const auto bom = frame->DocumentHasBom();
        editor->Send(Message::GotoPos, editor->Send(Message::GetLength));
        for (const char character : {'!', '?'}) {
            editor->Send(Message::BeginUndoAction);
            editor->Send(Message::AddText, 1, reinterpret_cast<intptr_t>(&character));
            editor->Send(Message::EndUndoAction);
        }
        editor->Send(Message::Undo);
        Require(editor->Send(Message::GetModify) && editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
                "display fixture starts dirty with both Undo and Redo pending");
        editor->Send(Message::SetSel, editor->Send(Message::GetLength), 0);
        editor->Send(Message::SetMultipleSelection, 1);
        editor->Send(Message::AddSelection, 0, 0);
        editor->Send(Message::SetZoom, 175);
        editor->SetIndentationWidths(8, 4);
        if (editor->Send(Message::GetWrapMode) == 0) editor->ToggleWordWrap();
        DisplayInvariant(frame);
        editor->Send(Message::SetReadOnly, 1);
        DisplayInvariant(frame);
        editor->Send(Message::SetReadOnly, 0);
        Require(editor->Send(Message::GetZoom) == 175 && editor->Send(Message::GetTabWidth) == 8 &&
                    editor->Send(Message::GetWrapMode) != 0 && editor->Send(Message::GetEOLMode) == eol &&
                    frame->DocumentHasBom() == bom && frame->DocumentEncoding() == fixture.encoding &&
                    frame->DocumentPath() == TestFiles::Filename(path) && TestFiles::Bytes(path) == fixture.before,
                "display actions preserve wrapped/zoomed/tabbed view and independent original encoded disk bytes");
        TestFiles::RequireSavedUndoRedo(frame, path, fixture,
                                        {"save observes independent encoded bytes after display actions",
                                         "saved Undo observes original encoded bytes",
                                         "saved Redo observes encoded edit after display actions"});
    }
    TestEditor::RequireVirtualRectangleUnchanged(
        editor, [frame] { DisplayInvariant(frame); }, "display actions preserve rectangular virtual space");
}
void Preferences(EditorFrame* frame, int guides, int edge, int column, const wxString& action) {
    auto* editor = frame->Editor();
    Require(editor->Send(Message::GetIndentationGuides) == guides,
            "imported ShowIndentGuides/SimpleIndentGuides selects original mode");
    if (guides != 0) {
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "C/C++"));
        Require(editor->Send(Message::GetIndentationGuides) == (guides == 1 ? 1 : 3),
                "imported simple mode survives language changes");
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Plain Text"));
    }
    Require(editor->Send(Message::GetEdgeMode) == edge && editor->Send(Message::GetEdgeColumn) == column,
            "imported long-line mode, visibility and limit reach the real editor");
    if (action == "toggle" || action == "explicit") {
        Command(frame, "View", "Indentation Guides");
        Command(frame, "View", "Long Line Marker");
        if (action == "explicit") Command(frame, "Settings", "Save Settings Now");
    } else if (action == "dialog")
        LongLineDialog(frame, wxString::Format("%d", column), "35", 2);
}
class GuideApp final : public wxApp {
public:
    bool OnInit() override {
        const bool probe = argc >= 6 && argv[1] == "--preferences";
        if (probe) settings_ = std::make_unique<SettingsStore>();
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame, probe](bool presented) {
            try {
                Require(presented, "private compositor must present the owned editor before native scenarios");
                if (probe) {
                    if (argc == 7)
                        Require(frame->Open(argv[6]).status == OpenStatus::Opened,
                                "open imported file-variable preference fixture");
                    Preferences(frame, wxAtoi(argv[2]), wxAtoi(argv[3]), wxAtoi(argv[4]), argv[5]);
                } else {
                    Guides(frame);
                    Marker(frame);
                    DialogAndModeline(frame);
                    SavedHistoryAndViews(frame);
                }
                std::puts("PASS: original indentation guide commands");
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                result_ = 1;
            }
            frame->Editor()->Send(Message::SetSavePoint);
            if (probe)
                frame->Close();
            else
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
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(GuideApp);
