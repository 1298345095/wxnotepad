#include "editor/editor_control.h"
#include <wx/wx.h>
#include <imm.h>
#include <cstdio>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void CheckGeometry(EditorControl* editor) {
    using Scintilla::Message;
    const auto position = editor->Send(Message::GetCurrentPos);
    const auto x = editor->Send(Message::PointXFromPosition, 0, position);
    const auto y = editor->Send(Message::PointYFromPosition, 0, position);
    const auto height = editor->Send(Message::TextHeight, 0);
    const auto window = static_cast<HWND>(editor->GetHandle());
    const auto context = ImmGetContext(window);
    Require(context != nullptr, "the focused editor must have an IMM context");
    COMPOSITIONFORM composition{};
    CANDIDATEFORM candidate{};
    LOGFONTW font{};
    const bool read = ImmGetCompositionWindow(context, &composition) && ImmGetCandidateWindow(context, 0, &candidate) &&
                      ImmGetCompositionFontW(context, &font);
    ImmReleaseContext(window, context);
    std::printf("IME caret=%lld:%lld composition=%ld:%ld candidate=%ld:%ld height=%lld\n", static_cast<long long>(x),
                static_cast<long long>(y), composition.ptCurrentPos.x, composition.ptCurrentPos.y,
                candidate.ptCurrentPos.x, candidate.ptCurrentPos.y, static_cast<long long>(height));
    Require(read, "IMM must expose composition, candidate and font geometry");
    Require(composition.dwStyle == CFS_POINT && composition.ptCurrentPos.x == x && composition.ptCurrentPos.y == y,
            "composition must start at the core caret position");
    Require(candidate.dwStyle == CFS_EXCLUDE && candidate.ptCurrentPos.x == x &&
                candidate.ptCurrentPos.y == y + height && candidate.rcArea.left == x && candidate.rcArea.top == y &&
                candidate.rcArea.right == x + 1 && candidate.rcArea.bottom == y + height,
            "candidate must exclude the caret line");
    Require(font.lfHeight < 0 && font.lfFaceName[0] != 0, "composition must use an explicit editor font");
}

class ImeApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new wxFrame(nullptr, wxID_ANY, "Windows IME geometry check", wxDefaultPosition, wxSize(640, 320));
        auto* editor = new EditorControl(frame);
        auto* otherFocus = new wxButton(frame, wxID_ANY, "Focus target");
        auto* layout = new wxBoxSizer(wxVERTICAL);
        layout->Add(editor, 1, wxEXPAND);
        layout->Add(otherFocus, 0);
        frame->SetSizer(layout);
        frame->Show();
        editor->SetFocus();
        if (argc == 2 && wxString(argv[1]) == "--manual") {
            // A human-only fixture, kept out of the application. Positioning
            // uses the real control's public API without navigation-key mappings.
            std::string blankLines;
            for (int line = 1; line < 100; ++line)
                blankLines += "\r\n";
            editor->Send(Scintilla::Message::SetText, 0, reinterpret_cast<intptr_t>(blankLines.c_str()));
            editor->Send(Scintilla::Message::EmptyUndoBuffer);
            editor->Send(Scintilla::Message::SetSavePoint);
            editor->Send(Scintilla::Message::GotoPos, 0);
            auto* positions = new wxBoxSizer(wxHORIZONTAL);
            const auto addPosition = [frame, editor, positions](const char* label, int line) {
                auto* button = new wxButton(frame, wxID_ANY, label);
                positions->Add(button, 0);
                button->Bind(wxEVT_BUTTON, [editor, line](wxCommandEvent&) {
                    const auto position = editor->Send(Scintilla::Message::PositionFromLine, line);
                    editor->Send(Scintilla::Message::GotoPos, position);
                    editor->Send(Scintilla::Message::ScrollCaret);
                    editor->SetFocus();
                });
            };
            addPosition("First line", 0);
            addPosition("Second line", 1);
            addPosition("Scrolled last line", 99);
            layout->Insert(1, positions, 0, wxEXPAND);
            frame->Layout();
            frame->SetTitle("Microsoft Pinyin manual check - 100 blank lines");
            result_ = 0;
            return true;
        }
        CallAfter([this, frame, editor, otherFocus] {
            try {
                using Scintilla::Message;
                wxYield();
                Require(editor->HasFocus(), "the editor must receive native focus");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("first line\r\nsecond line"));
                editor->Send(Message::GotoPos, 5);
                editor->Update();
                CheckGeometry(editor);
                std::puts("PASS: first-line native IME composition/candidate position and font");
                editor->Send(Message::GotoPos, 16);
                Require(editor->Send(Message::PointYFromPosition, 0, 16) > 0,
                        "the second-line sample must exercise a different vertical point");
                editor->Update();
                CheckGeometry(editor);
                std::puts("PASS: second-line native IME position");
                std::string lines;
                for (int line = 0; line < 80; ++line)
                    lines += "scroll sample\r\n";
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(lines.c_str()));
                const auto position = editor->Send(Message::PositionFromLine, 40) + 6;
                editor->Send(Message::GotoPos, position);
                editor->Send(Message::SetFirstVisibleLine, 38);
                Require(editor->Send(Message::GetFirstVisibleLine) == 38, "the scroll sample must move the viewport");
                editor->Update();
                CheckGeometry(editor);
                std::puts("PASS: scrolled native IME position");
                otherFocus->SetFocus();
                wxYield();
                Require(otherFocus->HasFocus(), "focus must leave the editor");
                editor->Send(Message::GotoPos, position + 3);
                editor->SetFocus();
                wxYield();
                Require(editor->HasFocus(), "the editor must regain native focus");
                CheckGeometry(editor);
                std::puts("PASS: native IME position after refocus");
                editor->Send(Message::GotoPos, position + 5);
                SendMessageW(static_cast<HWND>(editor->GetHandle()), WM_IME_STARTCOMPOSITION, 0, 0);
                CheckGeometry(editor);
                SendMessageW(static_cast<HWND>(editor->GetHandle()), WM_IME_ENDCOMPOSITION, 0, 0);
                std::puts("PASS: native composition start refreshes a pending caret move");
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                result_ = 1;
            }
            frame->Destroy();
            ExitMainLoop();
        });
        return true;
    }
    int OnExit() override { return result_; }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(ImeApp);
