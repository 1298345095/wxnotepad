#include "native_key_helpers.h"
#include "settings/settings_store.h"
#include "syntax/syntax_scheme.h"
#include <wx/checkbox.h>
#include <wx/button.h>
#include <wx/modalhook.h>
#include <wx/wx.h>
#include <cstdio>
#include <stdexcept>
namespace {
using Scintilla::Message;
constexpr int PreservedBit = 1 << 15;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Character(EditorControl* editor, int ch) {
    wxKeyEvent event(wxEVT_CHAR);
    event.m_keyCode = ch;
    event.m_uniChar = ch;
    event.SetEventObject(editor);
    editor->GetEventHandler()->ProcessEvent(event);
}
void Reset(EditorControl* editor, const char* text) {
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(text));
    editor->Send(Message::GotoPos, editor->Send(Message::GetLength));
    editor->Send(Message::Colourise, 0, -1);
}
void Typing(EditorFrame* frame, int bit, bool enabled) {
    auto* editor = frame->Editor();
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "C/C++"));
    const char openings[] = "({[<\"'`,";
    if (bit < 256) {
        int index = 0;
        while ((1 << index) != bit)
            ++index;
        Reset(editor, index == 3 ? "int" : "");
        Character(editor, openings[index]);
        const std::string expected =
            index == 3 ? (enabled ? "int<>" : "int<")
                       : std::string(1, openings[index]) + (enabled ? std::string(1, ")}]>\"'` "[index]) : "");
        Require(NativeSearch::Text(editor) == expected, "each persisted bit controls live character typing");
    } else {
        Reset(editor, "word");
        editor->Send(Message::SetSel, 0, 0);
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Toggle Line Comment"));
        Require(NativeSearch::Text(editor) == (enabled ? "// word" : "//word"),
                "same preference controls line comment spacing");
    }
}
class DialogDriver final : public wxModalDialogHook {
public:
    DialogDriver(std::function<void(wxDialog*)> action) : action_(std::move(action)) { Register(); }
    int Enter(wxDialog* dialog) override {
        if (dialog->GetTitle() == "Auto Completion Settings")
            wxTheApp->CallAfter([this, dialog] {
                NativeKeys::AfterPresentation(dialog, [this, dialog](bool presented) {
                    Require(presented, "owned settings dialog is presented");
                    action_(dialog);
                });
            });
        return wxID_NONE;
    }

private:
    std::function<void(wxDialog*)> action_;
};
wxCheckBox* Check(wxDialog* dialog, const char* name) {
    auto* check = wxDynamicCast(wxWindow::FindWindowByName(name, dialog), wxCheckBox);
    Require(check, "original native automatic insertion control exists");
    return check;
}
void Click(wxWindow* control) {
#ifdef __WXMSW__
    SendMessageW(static_cast<HWND>(control->GetHandle()), BM_CLICK, 0, 0);
#else
    gtk_button_clicked(GTK_BUTTON(control->GetHandle()));
#endif
}
void Visit(EditorFrame* frame, const std::function<void(wxDialog*)>& action) {
    const int command = frame->GetMenuBar()->FindMenuItem("Settings", "Auto Completion Settings...");
    Require(command != wxNOT_FOUND, "Settings exposes native Auto Completion Settings");
    bool opened = false;
    DialogDriver driver([&](wxDialog* dialog) {
        opened = true;
        action(dialog);
    });
    NativeSearch::Command(frame, command);
    Require(opened, "settings dialog opens");
}
class SettingsApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        auto* frame = new EditorFrame(settings_.get());
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            try {
                long bit = 0;
                Require(argc == 3 && argv[2].ToLong(&bit), "settings test requires mode and original bit");
                const bool reopen = argv[1] == "--reopen";
                const char* labels[] = {"Parentheses ()",    "Braces {}",         "Square brackets []",
                                        "Angle brackets <>", "Double quotes",     "Single quotes",
                                        "Backticks",         "Space after comma", "Space after comment"};
                int index = 0;
                while ((1 << index) != bit)
                    ++index;
                if (reopen) {
                    Require(settings_->values.autoInsertMask == (PreservedBit | (511 ^ bit)),
                            "saved original bits reopen exactly");
                    Typing(frame, bit, false);
                    Require(!frame->Editor()->AutoIndentEnabled(), "Auto Indent saved setting reopens");
                } else {
                    Require(settings_->values.autoInsertMask == (PreservedBit | 511),
                            "imports original insertion bits");
                    Typing(frame, bit, true);
                    Visit(frame, [&](wxDialog* dialog) {
                        for (const auto* name : labels)
                            Require(Check(dialog, name)->GetValue(), "native controls reflect imported bits");
                        Click(Check(dialog, labels[index]));
                        Click(Check(dialog, "Auto Indent"));
                        Click(dialog->FindWindow(wxID_CANCEL));
                    });
                    Require(settings_->values.autoInsertMask == (PreservedBit | 511) &&
                                frame->Editor()->AutoIndentEnabled(),
                            "Cancel commits no delivered preference");
                    Visit(frame, [&](wxDialog* dialog) {
                        Click(Check(dialog, labels[index]));
                        Click(Check(dialog, "Auto Indent"));
                        Click(dialog->FindWindow(wxID_OK));
                    });
                    Require(settings_->values.autoInsertMask == (PreservedBit | (511 ^ bit)),
                            "OK changes exactly one delivered bit");
                    Require(!settings_->values.autoIndent && !frame->Editor()->AutoIndentEnabled(),
                            "same Auto Indent preference changes live");
                    Typing(frame, bit, false);
                    Require(settings_->Save(true), "existing settings save succeeds");
                }
                frame->Editor()->Send(Message::SetSavePoint);
                result_ = 0;
                std::puts("PASS: native insertion controls, Cancel/OK, live typing and settings persistence");
            } catch (const std::exception& e) {
                std::fprintf(stderr, "FAIL: %s\n", e.what());
            }
            frame->Destroy();
            ExitMainLoop();
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }
    int OnExit() override { return result_; }

private:
    int result_ = 1;
    std::unique_ptr<SettingsStore> settings_;
};
} // namespace
wxIMPLEMENT_APP(SettingsApp);
