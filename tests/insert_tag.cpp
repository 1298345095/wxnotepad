#include "native_key_helpers.h"
#include "encoded_file_helpers.h"
#include <wx/textctrl.h>
#include <wx/stopwatch.h>
#include <exception>
#include <functional>
#include <fstream>
#include <filesystem>
#include <map>
#include <sstream>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
class TagDriver final : public wxModalDialogHook {
public:
    explicit TagDriver(std::function<void(wxDialog*)> action, bool rectangle = false)
        : action_(std::move(action)), rectangle_(rectangle) {
        Register();
    }
    int Enter(wxDialog* dialog) override {
        if (dialog->GetTitle() == "Insert HTML/XML Tag")
            wxTheApp->CallAfter([this, dialog] { action_(dialog); });
        else if (rectangle_ && dialog->GetTitle() == "wxnotepad") {
            ++warnings;
            return wxID_OK;
        }
        return wxID_NONE;
    }
    int warnings = 0;

private:
    std::function<void(wxDialog*)> action_;
    bool rectangle_;
};
wxTextCtrl* Field(wxDialog* dialog, const char* name) {
    auto* input = wxDynamicCast(wxWindow::FindWindowByName(name, dialog), wxTextCtrl);
    Require(input && input->IsMultiLine() && input->IsEditable(), "original tag fields are editable multiline inputs");
    return input;
}
void Edit(wxTextCtrl* input, const wxString& value) {
#ifdef __WXMSW__
    const auto handle = static_cast<HWND>(input->GetHandle());
    const auto native = value.wc_str();
    SendMessageW(handle, EM_SETSEL, 0, -1);
    SendMessageW(handle, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(static_cast<const wchar_t*>(native)));
#elif defined(__WXGTK__)
    auto* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(input->GetConnectWidget()));
    gtk_text_buffer_set_text(buffer, value.utf8_str(), -1);
#endif
    wxYield();
}
int Dialog(EditorFrame* frame, int id, const std::function<void(wxDialog*)>& action, bool shortcut = false,
           bool rectangle = false) {
    bool observed = false;
    bool released = false;
    std::exception_ptr failure;
    TagDriver driver(
        [&](wxDialog* dialog) {
            observed = true;
            try {
#ifndef __WXMSW__
                wxStopWatch wait;
                while (shortcut && !released && wait.Time() < 3000) {
                    wxYield();
                    wxMilliSleep(10);
                }
                Require(!shortcut || released, "opening Alt+X releases inside the owned tag dialog");
#endif
                auto* opening = Field(dialog, "Opening tag");
                auto* closing = Field(dialog, "Closing tag");
                wxStopWatch focusWait;
                while (!opening->HasFocus() && focusWait.Time() < 3000) {
                    wxYield();
                    wxMilliSleep(10);
                }
                long from = 0;
                long to = 0;
                opening->GetSelection(&from, &to);
                if (!opening->HasFocus() || from != 1 || to != 4)
                    std::fprintf(stderr, "Initial tag focus=%d active=%d selection=%ld/%ld\n", opening->HasFocus(),
                                 dialog->IsActive(), from, to);
                Require((dialog->GetWindowStyle() & wxRESIZE_BORDER) && opening->GetValue() == "<tag>" &&
                            closing->GetValue() == "</tag>" && from == 1 && to == 4 && opening->HasFocus(),
                        "every original tag opening starts fresh with the focused name selected");
                action(dialog);
            } catch (...) {
                failure = std::current_exception();
                dialog->EndModal(wxID_CANCEL);
            }
        },
        rectangle);
    if (shortcut)
        NativeKeys::Shortcut(frame, 'X', false, true, false, &released);
    else
        NativeSearch::Command(frame, id);
    Require(observed, "native tag command opens the shared dialog");
    if (failure) std::rethrow_exception(failure);
    Require(frame->Editor()->HasFocus(), "tag dialog returns focus to the editor");
    return driver.warnings;
}
void FieldKey(wxTextCtrl* input, int key, bool shift = false) {
    input->SetFocus();
    wxYield();
#ifdef __WXMSW__
    if (key == WXK_TAB) {
        NativeKeys::DialogNavigation(wxDynamicCast(wxGetTopLevelParent(input), wxDialog), VK_TAB);
        return;
    }
    BYTE previous[256];
    Require(GetKeyboardState(previous), "read owned native field key state");
    BYTE modified[256];
    std::copy(std::begin(previous), std::end(previous), std::begin(modified));
    modified[VK_SHIFT] = shift ? 0x80 : 0;
    modified[VK_CONTROL] = modified[VK_MENU] = 0;
    Require(SetKeyboardState(modified), "set owned native field key modifiers");
    const auto handle = static_cast<HWND>(input->GetHandle());
    SendMessageW(handle, WM_KEYDOWN, VK_RETURN, 0);
    if (shift) SendMessageW(handle, WM_CHAR, VK_RETURN, 0);
    SendMessageW(handle, WM_KEYUP, VK_RETURN, 0);
    SetKeyboardState(previous);
#else
    wxString focusFile;
    Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile), "native dialog keys use the private display");
    const auto before = input->GetValue();
    auto* dialog = wxGetTopLevelParent(input);
    const auto completed = [&] {
        if (key == WXK_TAB) return !input->HasFocus();
        return shift ? input->GetValue() != before : !dialog->IsShown();
    };
    {
        wxFFile request(focusFile, "wb");
        request.Write(wxString("key:") + (shift ? "shift+" : "") + (key == WXK_TAB ? "Tab" : "Return"));
    }
    wxStopWatch wait;
    while (!completed() && wait.Time() < 3000) {
        wxYield();
        wxMilliSleep(10);
    }
    // Native GTK dialog handling can consume key-up; verify its actual field/dialog result.
    if (!completed())
        std::fprintf(stderr, "Native field key timeout: key=%d shift=%d focus=%s value=%s\n", key, shift,
                     wxWindow::FindFocus() ? wxWindow::FindFocus()->GetName().utf8_str().data() : "none",
                     input->GetValue().utf8_str().data());
    Require(completed(), "native field key produces its original dialog outcome");
#endif
    wxYield();
}
void Keyboard(EditorFrame* frame, int id) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "x", 0, 1, Scintilla::EndOfLine::CrLf);
    Dialog(
        frame, id,
        [](wxDialog* dialog) {
            auto* opening = Field(dialog, "Opening tag");
            Edit(opening, "<tag>");
            FieldKey(opening, WXK_RETURN, true);
            Require(dialog->IsShown() && opening->GetValue() == "<tag>\n" &&
                        Field(dialog, "Closing tag")->GetValue() == "</tag>",
                    "original Shift+Enter inserts a field line break and regenerates closing");
            FieldKey(opening, WXK_TAB);
            auto* closing = Field(dialog, "Closing tag");
            Require(closing->HasFocus(), "original native Tab moves from opening to closing field");
            FieldKey(closing, WXK_RETURN);
            Require(!dialog->IsShown(), "original unmodified Enter accepts the tag dialog");
        },
        true);
    Require(TestEditor::Text(editor) == "<tag>\r\nx</tag>", "native Alt+X and Enter enclose with original CRLF bytes");
    bool opened = false;
    TagDriver driver([&](wxDialog* dialog) {
        opened = true;
        NativeSearch::Click(dialog, wxID_CANCEL);
    });
    for (const auto& [control, shift] : {std::pair{true, false}, std::pair{false, true}}) {
        TestEditor::ResetStream(editor, "x", 0, 1);
        NativeKeys::Shortcut(frame, 'X', control, true, shift);
        Require(!opened, "Ctrl+Alt+X and Alt+Shift+X retain conversion commands");
    }
    std::puts("PASS: native Alt+X, exact modifiers and original Enter/Shift+Enter");
}
void Regeneration(EditorFrame* frame, int id) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "x", 0, 1);
    Dialog(frame, id, [](wxDialog* dialog) {
        auto* opening = Field(dialog, "Opening tag");
        auto* closing = Field(dialog, "Closing tag");
        Edit(opening, "<DiV class='main'>");
        Require(closing->GetValue() == "</DiV>", "native opening edit regenerates closing and preserves tag case");
        NativeSearch::Click(dialog, wxID_OK);
    });
    Require(TestEditor::Text(editor) == "<DiV class='main'>x</DiV>", "native OK encloses the original selection");
}
std::string Escaped(std::string_view value) {
    std::string result;
    for (char ch : value) {
        switch (ch) {
        case '\\':
            result += "\\\\";
            break;
        case '\t':
            result += "\\t";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\n':
            result += "\\n";
            break;
        default:
            result += ch;
            break;
        }
    }
    return result;
}
wxString FieldText(std::string_view value) {
    if (value == "\"\"") return {};
    std::string text;
    for (size_t index = 0; index < value.size(); ++index) {
        char ch = value[index];
        if (ch == '\\' && index + 1 < value.size()) {
            ch = value[++index];
            if (ch == 'n')
                ch = '\n';
            else if (ch == 'r')
                ch = '\r';
            else if (ch == 't')
                ch = '\t';
            else
                Require(ch == '\\', "reference field uses a supported text escape");
        }
        text += ch;
    }
    return wxString::FromUTF8(text);
}
void ScannerCaptures(EditorFrame* frame, int id) {
    auto* editor = frame->Editor();
    for (const char* language : {"Plain Text", "C/C++", "Python", "JSON"}) {
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", language));
        TestEditor::ResetStream(editor, "x", 0, 1);
        Dialog(frame, id, [&](wxDialog* dialog) {
            auto* opening = Field(dialog, "Opening tag");
            auto* closing = Field(dialog, "Closing tag");
            std::ifstream input{std::filesystem::path(TAG_REFERENCE_FIXTURES)};
            Require(input.good(), "independent original scanner observations are available");
            size_t count = 0;
            for (std::string line; std::getline(input, line);) {
                if (line.empty() || line.front() == '#') continue;
                const auto first = line.find('\t');
                if (line.substr(0, first) != language) continue;
                const auto second = line.find('\t', first + 1);
                Require(second != std::string::npos, "scanner observation has opening and closing fields");
                Edit(opening, FieldText(std::string_view(line).substr(first + 1, second - first - 1)));
                const auto expected = FieldText(std::string_view(line).substr(second + 1));
                if (closing->GetValue() != expected)
                    std::fprintf(stderr, "Scanner capture mismatch: %s\n", line.c_str());
                Require(closing->GetValue() == expected, "native closing field matches the original scanner capture");
                ++count;
            }
            Require(count > 50, "each delivered language runs the independent scanner matrix");
            Edit(opening, "<first>");
            Edit(closing, "</MANUAL>");
            Require(closing->GetValue() == "</MANUAL>", "explicit closing edit remains editable");
            Edit(opening, "<second>");
            Require(closing->GetValue() == "</second>", "opening edit overwrites a manually edited closing");
            Edit(closing, "manual");
            Edit(opening, "<second/>");
            Require(closing->IsEmpty(), "invalid opening edit clears a manually edited closing");
            NativeSearch::Click(dialog, wxID_CANCEL);
        });
        Require(TestEditor::Text(editor) == "x" && !editor->Send(Scintilla::Message::CanUndo),
                "scanner edits and Cancel leave the original document and history");
    }
    std::puts("PASS: independent scanner captures and manual closing edits in all delivered languages");
}
std::string Snapshot(EditorControl* editor) {
    using Scintilla::Message;
    std::ostringstream result;
    result << Escaped(TestEditor::Text(editor));
    const auto add = [&](Message message, uintptr_t index = 0) { result << '\t' << editor->Send(message, index); };
    for (auto message :
         {Message::GetAnchor, Message::GetCurrentPos, Message::GetModify, Message::CanUndo, Message::CanRedo,
          Message::GetSelectionMode, Message::GetMainSelection, Message::GetSelections})
        add(message);
    for (intptr_t index = 0; index < editor->Send(Message::GetSelections); ++index)
        for (auto message : {Message::GetSelectionNAnchor, Message::GetSelectionNCaret,
                             Message::GetSelectionNAnchorVirtualSpace, Message::GetSelectionNCaretVirtualSpace})
            add(message, index);
    for (auto message :
         {Message::GetRectangularSelectionAnchor, Message::GetRectangularSelectionCaret,
          Message::GetRectangularSelectionAnchorVirtualSpace, Message::GetRectangularSelectionCaretVirtualSpace})
        add(message);
    return result.str();
}
void CloseWindow(wxDialog* dialog) {
#ifdef __WXMSW__
    SendMessageW(static_cast<HWND>(dialog->GetHandle()), WM_CLOSE, 0, 0);
#elif defined(__WXGTK__)
    gtk_window_close(GTK_WINDOW(dialog->GetHandle()));
#endif
    wxYield();
}
void OriginalStates(EditorFrame* frame, int id) {
    using Scintilla::Message;
    std::ifstream input{std::filesystem::path(TAG_STATE_REFERENCE_FIXTURES)};
    Require(input.good(), "independent original selection/history observations are available");
    std::map<std::string, std::string> references;
    for (std::string line; std::getline(input, line);) {
        if (line.empty() || line.front() == '#') continue;
        const auto separator = line.find('\t', line.find('\t') + 1);
        Require(separator != std::string::npos, "state capture identifies scenario and stage");
        references.emplace(line.substr(0, separator), line.substr(separator + 1));
    }
    auto* editor = frame->Editor();
    const auto check = [&](const std::string& kind, const char* stage) {
        const auto actual = Snapshot(editor);
        const auto expected = references.at(kind + '\t' + stage);
        if (actual != expected)
            std::fprintf(stderr, "Original %s %s\nExpected: %s\nObserved: %s\n", kind.c_str(), stage, expected.c_str(),
                         actual.c_str());
        Require(actual == expected,
                "tag command matches original document, selection, virtual space and history state");
    };
    for (const std::string kind : {"caret", "normal", "reverse", "multiline", "multiple", "virtual", "rectangle",
                                   "rectangle-reverse", "readonly", "cancel", "close", "empty"}) {
        editor->Send(Message::SetMultipleSelection, 0);
        TestEditor::ResetStream(editor, "A中😀B\nlast", kind == "reverse" ? 8 : 1,
                                kind == "reverse" || kind == "caret" ? 1 : 8);
        if (kind == "multiline") editor->Send(Message::SetSel, 14, 0);
        if (kind == "multiple") {
            editor->Send(Message::SetMultipleSelection, 1);
            editor->Send(Message::AddSelection, 10, 14);
            editor->Send(Message::SetMainSelection, 1);
        }
        if (kind == "virtual") {
            editor->Send(Message::SetSel, 14, 14);
            editor->Send(Message::SetSelectionNCaretVirtualSpace, 0, 2);
            editor->Send(Message::SetSelectionNAnchorVirtualSpace, 0, 4);
        }
        const bool rectangle = kind.starts_with("rectangle");
        if (rectangle) {
            TestEditor::ResetStream(editor, "xxx\nxxx", 1, 7);
            editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
            editor->Send(Message::SetRectangularSelectionAnchor, kind == "rectangle-reverse" ? 7 : 1);
            editor->Send(Message::SetRectangularSelectionCaret, kind == "rectangle-reverse" ? 1 : 7);
            editor->Send(Message::SetRectangularSelectionAnchorVirtualSpace, 2);
            editor->Send(Message::SetRectangularSelectionCaretVirtualSpace, 4);
        }
        if (kind == "readonly" || kind == "cancel" || kind == "close" || kind == "empty") {
            editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
            editor->Send(Message::SetSel, 1, 8);
            editor->Send(Message::Undo);
            editor->Send(Message::SetSel, 1, 8);
        }
        if (kind == "readonly") editor->Send(Message::SetReadOnly, 1);
        check(kind, "before");
        const int warnings = Dialog(
            frame, id,
            [&](wxDialog* dialog) {
                Edit(Field(dialog, "Opening tag"), kind == "empty" ? wxString{} : wxString::FromUTF8("<DiV a=\"😀\">"));
                Edit(Field(dialog, "Closing tag"), kind == "empty" ? "" : "</MANUAL>");
                if (kind == "close")
                    CloseWindow(dialog);
                else
                    NativeSearch::Click(dialog, kind == "cancel" ? wxID_CANCEL : wxID_OK);
            },
            false, rectangle);
        Require(warnings == static_cast<int>(rectangle), "rectangular acceptance shows the original warning once");
        check(kind, "after");
        editor->Send(Message::Undo);
        check(kind, "undo");
        editor->Send(Message::Redo);
        check(kind, "redo");
    }
    editor->Send(Message::SetReadOnly, 0);
    std::puts("PASS: independent original caret, direction, multiline, multiple, virtual, rectangle, read-only and "
              "Cancel history");
}
void EmbeddedLineEndings(EditorFrame* frame) {
    using Scintilla::Message;
    // Literal text from original.json eol captures, through the shared public enclosure operation.
    struct Capture {
        Scintilla::EndOfLine mode;
        const char* after;
    };
    const Capture captures[] = {
        {Scintilla::EndOfLine::CrLf, "<a>\r\n前\r後\n😀x</a>\r\n末\r尾\n𐐀"},
        {Scintilla::EndOfLine::Cr, "<a>\r前\r後\n😀x</a>\r末\r尾\n𐐀"},
        {Scintilla::EndOfLine::Lf, "<a>\n前\r後\n😀x</a>\n末\r尾\n𐐀"},
    };
    auto* editor = frame->Editor();
    for (const auto& capture : captures) {
        TestEditor::ResetStream(editor, "x", 0, 1, capture.mode);
        Require(
            editor->EncloseSelection(wxString::FromUTF8("<a>\r\n前\r後\n😀"), wxString::FromUTF8("</a>\r\n末\r尾\n𐐀")),
            "public enclosure accepts original Unicode tag fields");
        Require(TestEditor::Text(editor) == capture.after,
                "original embedded CRLF/lone CR/LF and supplementary bytes match");
        editor->Send(Message::Undo);
        Require(TestEditor::Text(editor) == "x" && !editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify),
                "one Undo restores original mixed-line-ending enclosure and save point");
        editor->Send(Message::Redo);
        Require(TestEditor::Text(editor) == capture.after, "one Redo restores original embedded line-ending bytes");
    }
    std::puts("PASS: independently captured Unicode, supplementary and embedded line-ending enclosure bytes");
}
void SavedBytes(EditorFrame* frame, int id) {
    const auto directory = TestFiles::TemporaryDirectory("insert-tag");
    std::filesystem::create_directory(directory);
    // Independent original saved/Undo/Redo bytes for each delivered language in original.json.
    const TestFiles::EncodedFixture capture{"tag", FileEncoding::Utf8, "A中😀B\nlast",
                                            "A<DiV a=\"😀\">中😀</MANUAL>B\nlast"};
    for (const auto& [extension, language] : {std::pair{"txt", "Plain Text"}, std::pair{"cpp", "C/C++"},
                                              std::pair{"py", "Python"}, std::pair{"json", "JSON"}}) {
        const auto path = directory / (std::string("tag.") + extension);
        TestFiles::WriteBytes(path, capture.before);
        frame->Editor()->Send(Scintilla::Message::SetSavePoint);
        Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "open native tag disk fixture");
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", language));
        frame->Editor()->Send(Scintilla::Message::SetSel, 1, 8);
        Dialog(frame, id, [](wxDialog* dialog) {
            Edit(Field(dialog, "Opening tag"), wxString::FromUTF8("<DiV a=\"😀\">"));
            Edit(Field(dialog, "Closing tag"), "</MANUAL>");
            NativeSearch::Click(dialog, wxID_OK);
        });
        TestFiles::RequireSavedUndoRedo(frame, path, capture,
                                        {"native tag acceptance saves independent original UTF-8 bytes",
                                         "one tag Undo saves independent original disk bytes",
                                         "one tag Redo saves independent original disk bytes"});
    }
    std::filesystem::remove_all(directory);
    std::puts("PASS: native accepted manual closing and independently read saved/Undo/Redo bytes in all languages");
}
class TagApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "private compositor must present the owned editor before native scenarios");
                auto* edit = frame->GetMenuBar()->GetMenu(frame->GetMenuBar()->FindMenu("Edit"));
                std::array<int, 2> tags{};
                size_t index = 0;
                for (const char* submenu : {"Insert", "Enclose Selection"}) {
                    auto* entry = edit->FindItem(edit->FindItem(submenu));
                    Require(entry && entry->GetSubMenu() &&
                                entry->GetSubMenu()->FindItem("HTML/XML Tag...") != wxNOT_FOUND,
                            "both original native submenus expose HTML/XML Tag");
                    tags[index++] = entry->GetSubMenu()->FindItem("HTML/XML Tag...");
                }
                std::puts("PASS: original native tag entry points");
                for (int id : tags)
                    Regeneration(frame, id);
                Keyboard(frame, tags[0]);
                ScannerCaptures(frame, tags[1]);
                OriginalStates(frame, tags[0]);
                EmbeddedLineEndings(frame);
                SavedBytes(frame, tags[1]);
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                result_ = 1;
            }
            frame->Editor()->Send(Scintilla::Message::SetSavePoint);
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
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(TagApp);
