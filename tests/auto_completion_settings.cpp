#include "native_key_helpers.h"
#include "settings/settings_store.h"
#include "syntax/syntax_scheme.h"
#include <wx/checkbox.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/display.h>
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
    explicit DialogDriver(std::function<void(wxDialog*)> action) : action_(std::move(action)) { Register(); }
    int Enter(wxDialog* dialog) override {
        if (dialog->GetTitle() == "Auto Completion Settings")
            wxTheApp->CallAfter([this, dialog] {
                NativeKeys::AfterPresentation(dialog, [this, dialog](bool presented) {
                    try {
                        Require(presented, "owned settings dialog is presented");
                        const auto area = wxDisplay(wxDisplay::GetFromWindow(dialog)).GetClientArea();
                        std::printf("Dialog %dx%d; work area %dx%d; DPI %dx%d", dialog->GetSize().x,
                                    dialog->GetSize().y, area.width, area.height, dialog->GetDPI().x,
                                    dialog->GetDPI().y);
                        Require(area.Contains(dialog->GetScreenRect()),
                                "completion settings dialog fits native work area at active font/DPI");
                        for (auto* child : dialog->GetChildren())
                            if (child->IsShown() &&
                                (wxDynamicCast(child, wxCheckBox) || wxDynamicCast(child, wxTextCtrl) ||
                                 wxDynamicCast(child, wxButton)))
                                Require(
                                    dialog->GetScreenRect().Contains(child->GetScreenRect()),
                                    "every completion setting and dialog button remains visible at active font/DPI");
                        action_(dialog);
                    } catch (...) {
                        failure = std::current_exception();
                        dialog->EndModal(wxID_CANCEL);
                    }
                });
            });
        return wxID_NONE;
    }

    std::exception_ptr failure;

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
template <class Action> void Visit(EditorFrame* frame, const Action& action) {
    const int command = frame->GetMenuBar()->FindMenuItem("Settings", "Auto Completion Settings...");
    Require(command != wxNOT_FOUND, "Settings exposes native Auto Completion Settings");
    bool opened = false;
    DialogDriver driver([&opened, &action](wxDialog* dialog) {
        opened = true;
        action(dialog);
    });
    NativeSearch::Command(frame, command);
    if (driver.failure) std::rethrow_exception(driver.failure);
    Require(opened, "settings dialog opens");
}
void TagSettings(EditorFrame* frame, SettingsStore& settings, bool reopen) {
    constexpr int OriginalOptions = PreservedBit | 7;
    constexpr int DisabledOptions = OriginalOptions & ~1;
    auto* editor = frame->Editor();
    const auto type = [frame, editor](bool enabled) {
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Web Source Code"));
        Reset(editor, "<div");
        Character(editor, '>');
        Require(NativeSearch::Text(editor) == (enabled ? "<div></div>" : "<div>"), "tag option changes live typing");
    };
    Require(settings.values.completion.completeOption == (reopen ? DisabledOptions : OriginalOptions),
            "original completion options load exactly");
    if (reopen) {
        type(false);
        return;
    }
    const auto insertion = settings.values.autoInsertMask;
    const auto indent = settings.values.autoIndent;
    type(true);
    Visit(frame, [](wxDialog* dialog) {
        auto* tags = Check(dialog, "Auto close HTML and XML tags");
        Require(tags->GetValue(), "native option reflects original enabled bit");
        Click(tags);
        Click(dialog->FindWindow(wxID_CANCEL));
    });
    Require(settings.values.completion.completeOption == OriginalOptions, "Cancel preserves tag option");
    type(true);
    Visit(frame, [](wxDialog* dialog) {
        Click(Check(dialog, "Auto close HTML and XML tags"));
        Click(dialog->FindWindow(wxID_OK));
    });
    Require(settings.values.completion.completeOption == DisabledOptions, "OK changes only original CloseTags bit");
    Require(settings.values.autoInsertMask == insertion && settings.values.autoIndent == indent,
            "tag option preserves insertion and indent settings");
    type(false);
    Require(settings.Save(true), "tag option saves through shared settings store");
}
wxTextCtrl* Field(wxDialog* dialog, const char* name) {
    auto* field = wxDynamicCast(wxWindow::FindWindowByName(name, dialog), wxTextCtrl);
    Require(field, "original native word completion numeric/text control exists");
    return field;
}
void WordSettings(EditorFrame* frame, SettingsStore& settings, bool reopen) {
    auto& value = settings.values.completion;
    if (reopen) {
        Require(value.minWordLength == 2 && value.minNumberLength == 0 && value.visibleItemCount == 8 &&
                    value.scanWordsTimeout == 75,
                "word completion numeric values reopen");
        Require(value.completeOption == 24 && value.completeScope == 8 && value.scanWordScope == 8 &&
                    value.fillUpMask == 0 && value.fillUpPunctuation == "!?:!",
                "word completion flags and normalized punctuation reopen");
        return;
    }
    const char* options[] = {
        "Complete words",           "Scan words in document",   "Only words in document",     "English IME mode only",
        "Complete inside comments", "Complete inside strings",  "Complete inside plain text", "Scan inside comments",
        "Scan inside strings",      "Scan inside plain text",   "Complete with Enter",        "Complete with Tab",
        "Complete with Space",      "Complete with punctuation"};
    const auto edit = [&options](wxDialog* dialog) {
        for (const char* name : options)
            Click(Check(dialog, name));
        Click(Check(dialog, "Auto close HTML and XML tags"));
        Field(dialog, "Minimum word length")->SetValue("2");
        Field(dialog, "Minimum number length")->SetValue("0");
        Field(dialog, "Visible rows")->SetValue("0");
        Field(dialog, "Scan timeout")->SetValue("75 ms");
        Field(dialog, "Fill-up punctuation")->SetValue(wxString::FromUTF8("a!中? :!9"));
    };
    Visit(frame, [&edit](wxDialog* dialog) {
        edit(dialog);
        Click(dialog->FindWindow(wxID_CANCEL));
    });
    Require(value.completeOption == 7 && value.completeScope == 15 && value.scanWordScope == 15 &&
                value.fillUpMask == 15 && value.minWordLength == 1 && value.minNumberLength == 3 &&
                value.visibleItemCount == 16 && value.scanWordsTimeout == 500,
            "Cancel leaves every word completion preference untouched");
    Visit(frame, [&edit](wxDialog* dialog) {
        edit(dialog);
        Click(dialog->FindWindow(wxID_OK));
    });
    Require(value.minWordLength == 2 && value.minNumberLength == 0 && value.visibleItemCount == 8 &&
                value.scanWordsTimeout == 75,
            "OK commits original numeric lower clamps and timeout suffix");
    Require(value.completeOption == 24 && value.completeScope == 8 && value.scanWordScope == 8 &&
                value.fillUpMask == 0 && value.fillUpPunctuation == "!?:!",
            "OK commits scope bits and ASCII punctuation normalization");
    Visit(frame, [](wxDialog* dialog) {
        Field(dialog, "Scan timeout")->SetValue("invalid");
        Field(dialog, "Minimum word length")->SetValue("invalid");
        Click(dialog->FindWindow(wxID_OK));
    });
    Require(value.scanWordsTimeout == 75 && value.minWordLength == 1,
            "invalid timeout retains prior value while invalid unsigned word length clamps");
    Visit(frame, [](wxDialog* dialog) {
        Field(dialog, "Minimum word length")->SetValue("2");
        Click(dialog->FindWindow(wxID_OK));
    });
    Require(settings.Save(true), "word completion preferences persist through existing settings lifecycle");
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
                if (argv[1] == "--tags" || argv[1] == "--tags-reopen") {
                    TagSettings(frame, *settings_, argv[1] == "--tags-reopen");
                    frame->Editor()->Send(Message::SetSavePoint);
                    result_ = 0;
                    std::puts("PASS: tag option Cancel/OK, live typing and persistence");
                    frame->Destroy();
                    ExitMainLoop();
                    return;
                }
                if (argv[1] == "--words" || argv[1] == "--words-reopen") {
                    WordSettings(frame, *settings_, argv[1] == "--words-reopen");
                    frame->Editor()->Send(Message::SetSavePoint);
                    result_ = 0;
                    std::puts("PASS: native word completion dialog controls, Cancel/OK, normalization and reopen");
                    frame->Destroy();
                    ExitMainLoop();
                    return;
                }
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
                    Visit(frame, [&labels, index](wxDialog* dialog) {
                        for (const auto* name : labels)
                            Require(Check(dialog, name)->GetValue(), "native controls reflect imported bits");
                        Click(Check(dialog, labels[index]));
                        Click(Check(dialog, "Auto Indent"));
                        Click(dialog->FindWindow(wxID_CANCEL));
                    });
                    Require(settings_->values.autoInsertMask == (PreservedBit | 511) &&
                                frame->Editor()->AutoIndentEnabled(),
                            "Cancel commits no delivered preference");
                    Visit(frame, [&labels, index](wxDialog* dialog) {
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
