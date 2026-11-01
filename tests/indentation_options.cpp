#include "tab_settings_helpers.h"
#include "file_helpers.h"
#include <filesystem>

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Check(EditorControl* editor, int tab, int indent, bool spaces) {
    if (editor->Send(Message::GetTabWidth) != tab || editor->Send(Message::GetIndent) != indent ||
        editor->Send(Message::GetUseTabs) != !spaces)
        std::fprintf(stderr, "Policy actual=%lld/%lld/%lld expected=%d/%d/%d\n",
                     static_cast<long long>(editor->Send(Message::GetTabWidth)),
                     static_cast<long long>(editor->Send(Message::GetIndent)),
                     static_cast<long long>(editor->Send(Message::GetUseTabs)), tab, indent, !spaces);
    Require(editor->Send(Message::GetTabWidth) == tab && editor->Send(Message::GetIndent) == indent &&
                editor->Send(Message::GetUseTabs) == !spaces,
            "effective independent widths and spaces policy must follow original precedence");
}
void Language(EditorFrame* frame, const char* name) {
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", name));
}
void Reset(EditorControl* editor, const char* text, intptr_t anchor, intptr_t caret) {
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(text));
    editor->Send(Message::EmptyUndoBuffer);
    editor->Send(Message::SetSavePoint);
    editor->Send(Message::SetSel, anchor, caret);
}
void History(EditorControl* editor, const std::string& edited, const std::string& original) {
    if (NativeSearch::Text(editor) != edited)
        std::fprintf(stderr, "Actual='%s', expected='%s'\n", NativeSearch::Text(editor).c_str(), edited.c_str());
    Require(NativeSearch::Text(editor) == edited, "indent command must produce exact expected bytes");
    editor->Send(Message::Undo);
    Require(NativeSearch::Text(editor) == original && !editor->Send(Message::GetModify),
            "one Undo must restore original text and save point");
    editor->Send(Message::Redo);
    Require(NativeSearch::Text(editor) == edited, "one Redo must restore the indent edit");
}
class IndentationApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            const auto directory = std::filesystem::temp_directory_path() /
                                   ("wxnotepad-original-tabs-" + std::to_string(wxGetProcessId()));
            try {
                std::filesystem::create_directories(directory);
                auto* editor = frame->Editor();
                Check(editor, 4, 4, false);
                Require(editor->Send(Message::GetTabIndents) && editor->Send(Message::GetBackSpaceUnIndents) == 2,
                        "original global Tab and smart Backspace defaults must apply");
                for (const auto* language : {"C/C++", "Python", "Plain Text"}) {
                    Language(frame, language);
                    Check(editor, 4, 4, false);
                }
                Language(frame, "JSON");
                Check(editor, 2, 2, true);
                Language(frame, "Plain Text");
                TabDialog::Show(frame, [](wxDialog* dialog) {
                    TabDialog::Width(dialog, "Global Tab Width")->SetValue(7);
                    TabDialog::Width(dialog, "Global Indentation Width")->SetValue(3);
                    TabDialog::Toggle(TabDialog::Option(dialog, "Global Tabs as Spaces"), true);
                });
                Check(editor, 7, 3, true);
                for (const auto* language : {"C/C++", "Python", "Plain Text"}) {
                    Language(frame, language);
                    Check(editor, 7, 3, true);
                }
                Language(frame, "JSON");
                Check(editor, 2, 2, true);
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Tabs as Spaces"));
                Language(frame, "Python");
                Check(editor, 7, 3, false);
                Require(frame->New(), "empty New must succeed");
                Check(editor, 7, 3, true);
                std::puts("PASS: original defaults, global inheritance, JSON defaults, partial override and New reset");

                TabDialog::Show(frame, [](wxDialog* dialog) {
                    TabDialog::Toggle(TabDialog::Option(dialog, "Use Global Tab Settings"), false);
                    TabDialog::Width(dialog, "Scheme Tab Width")->SetValue(9);
                    TabDialog::Width(dialog, "Scheme Indentation Width")->SetValue(5);
                    TabDialog::Toggle(TabDialog::Option(dialog, "Scheme Tabs as Spaces"), false);
                });
                Check(editor, 9, 5, false);
                Language(frame, "Python");
                Check(editor, 7, 3, true);
                Language(frame, "Plain Text");
                Check(editor, 9, 5, false);
                TabDialog::File(frame, 8, 3, true);
                TabDialog::Show(frame, [](wxDialog* dialog) {
                    TabDialog::Toggle(TabDialog::Option(dialog, "Use Global Tab Settings"), true);
                    Require(!TabDialog::Width(dialog, "Scheme Tab Width")->IsEnabled(),
                            "global inheritance disables scheme widths");
                    TabDialog::Toggle(TabDialog::Option(dialog, "Use Global Tab Settings"), false);
                    Require(TabDialog::Width(dialog, "Scheme Tab Width")->GetValue() == 9 &&
                                TabDialog::Width(dialog, "Scheme Indentation Width")->GetValue() == 5,
                            "turning global inheritance off restores stored scheme widths");
                    TabDialog::Toggle(TabDialog::Option(dialog, "Use Scheme Tab Settings"), true);
                    TabDialog::Toggle(TabDialog::Option(dialog, "Use Scheme Tab Settings"), false);
                    Require(TabDialog::Width(dialog, "File Tab Width")->GetValue() == 8 &&
                                TabDialog::Width(dialog, "File Indentation Width")->GetValue() == 3,
                            "turning scheme inheritance off restores pre-dialog file widths");
                });
                Reset(editor, " \talpha\n", 1, 3);
                const auto bytes = NativeSearch::Text(editor);
                TabDialog::Show(
                    frame,
                    [](wxDialog* dialog) {
                        TabDialog::Width(dialog, "File Tab Width")->SetValue(256);
                        TabDialog::Width(dialog, "File Indentation Width")->SetValue(0);
                    },
                    false);
                Check(editor, 8, 3, true);
                Require(NativeSearch::Text(editor) == bytes && editor->Send(Message::GetAnchor) == 1 &&
                            editor->Send(Message::GetCurrentPos) == 3 && !editor->Send(Message::GetModify),
                        "cancel and preference changes preserve whitespace, selection and history");
                Language(frame, "JSON");
                Check(editor, 8, 3, true);
                const auto saved = directory / "saved.py";
                Require(frame->SaveFirst(TestFiles::Filename(saved), {}, SaveType::AllFiles).status ==
                                SaveStatus::Saved &&
                            TestFiles::Bytes(saved) == bytes,
                        "first Save keeps file overrides and exact whitespace bytes");
                Check(editor, 8, 3, true);
                const auto savedAs = directory / "saved.json";
                Require(frame->SaveAs(TestFiles::Filename(savedAs), {}, SaveType::AllFiles).status ==
                                SaveStatus::Saved &&
                            TestFiles::Bytes(savedAs) == bytes,
                        "Save As preserves bytes and file overrides");
                Check(editor, 8, 3, true);
                Require(frame->Open(TestFiles::Filename(savedAs)).status == OpenStatus::Opened, "reopen succeeds");
                Check(editor, 2, 2, true);
                Require(frame->Open(TestFiles::Filename(directory / "missing.py")).status == OpenStatus::Failed &&
                            frame->SaveAs(std::nullopt).status == SaveStatus::Cancelled,
                        "failed and cancelled operations retain settings");
                Check(editor, 2, 2, true);
                std::puts("PASS: per-scheme and file precedence, cancel, Open, first Save and Save As");

                TabDialog::File(frame, 8, 3, true);
                Reset(editor, " x", 1, 1);
                editor->Send(Message::Tab);
                NativeSearch::Selection(editor, 3, 3, "Tab caret follows indentation width");
                History(editor, "   x", " x");
                Reset(editor, "   x", 3, 3);
                editor->Send(Message::BackTab);
                NativeSearch::Selection(editor, 0, 0, "Shift+Tab caret follows unindent");
                History(editor, "x", "   x");
                Reset(editor, "      x", 6, 6);
                editor->Send(Message::DeleteBack);
                History(editor, "   x", "      x");
                TabDialog::Show(frame, [](wxDialog* dialog) {
                    TabDialog::Toggle(TabDialog::Option(dialog, "Backspace Unindents"), true);
                    TabDialog::Toggle(TabDialog::Option(dialog, "Backspace at Indentation Deletes up to Tab Stop"),
                                      false);
                });
                Check(editor, 8, 3, true);
                Reset(editor, "      x", 6, 6);
                editor->Send(Message::DeleteBack);
                History(editor, "   x", "      x");
                Reset(editor, "a\nb\n", 0, 3);
                editor->IndentLines(true);
                NativeSearch::Selection(editor, 0, 10, "block indentation preserves line coverage");
                History(editor, "   a\n   b\n", "a\nb\n");
                editor->Send(Message::SelectAll);
                editor->IndentLines(false);
                Require(NativeSearch::Text(editor) == "a\nb\n", "block unindent uses independent step");
                TabDialog::Show(frame, [](wxDialog* dialog) {
                    TabDialog::Toggle(TabDialog::Option(dialog, "Tab Indents"), false);
                });
                Check(editor, 8, 3, true);
                Reset(editor, " x", 1, 1);
                editor->Send(Message::Tab);
                History(editor, "        x", " x");
                TabDialog::File(frame, 8, 0, false);
                Check(editor, 8, 0, false);
                Reset(editor, "x", 1, 1);
                editor->Send(Message::Tab);
                History(editor, "x\t", "x");
                TabDialog::File(frame, 256, 256, true);
                Check(editor, 256, 256, true);
                Reset(editor, " \talpha\n", 1, 3);
                editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
                editor->Send(Message::Undo);
                editor->Send(Message::SetSel, 1, 3);
                TabDialog::File(frame, 5, 2, false);
                Require(NativeSearch::Text(editor) == " \talpha\n" && !editor->Send(Message::GetModify) &&
                            editor->Send(Message::CanRedo) && editor->Send(Message::GetAnchor) == 1 &&
                            editor->Send(Message::GetCurrentPos) == 3,
                        "applied preferences preserve nonempty text, selection, save point and pending Redo");
                editor->Send(Message::Redo);
                Require(NativeSearch::Text(editor) == " \talpha\n!", "pending Redo remains usable after options");
                editor->Send(Message::Undo);
                Require(frame->New(), "saved New must reset overrides");
                Check(editor, 9, 5, false);
                Reset(editor, " \tdata\n", 0, 0);
                const auto firstJson = directory / "first.json";
                Require(frame->SaveFirst(TestFiles::Filename(firstJson), {}, SaveType::AllFiles).status ==
                                SaveStatus::Saved &&
                            TestFiles::Bytes(firstJson) == " \tdata\n",
                        "nonempty first Save applies filename scheme without converting whitespace");
                Check(editor, 2, 2, true);
                const auto plain = directory / "plain.txt";
                Require(frame->SaveAs(TestFiles::Filename(plain), {}, SaveType::AllFiles).status == SaveStatus::Saved,
                        "Save As applies the new filename scheme");
                Check(editor, 9, 5, false);
                std::puts("PASS: Tab, Shift+Tab, Backspace, block indent, independent widths, selection and Undo/Redo");
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                result_ = 1;
            }
            std::filesystem::remove_all(directory);
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
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(IndentationApp);
