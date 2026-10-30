#include "editor_control.h"
#include <wx/wx.h>
#include <cstdio>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class SmokeApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new wxFrame(nullptr, wxID_ANY, "wxnotepad smoke", wxDefaultPosition, wxSize(800, 500));
        auto* editor = new EditorControl(frame);
        frame->Show();
        CallAfter([this, frame, editor] {
            try {
                using Scintilla::Message;
                const std::string expected = "Original Notepad4 core + wxWidgets\nASCII: read-only document\n中文：原版内核中的只读文本\n";
                const auto length = editor->Send(Message::GetLength);
                std::string text(static_cast<size_t>(length) + 1, '\0');
                editor->Send(Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
                text.resize(static_cast<size_t>(length));
                Require(text == expected, "the original document must contain the UTF-8 sample");
                Require(editor->Send(Message::GetReadOnly) == 1, "the document must be read-only");
                editor->Send(Message::ReplaceSel, 0, reinterpret_cast<intptr_t>("changed"));
                Require(editor->Send(Message::GetLength) == length, "read-only commands must preserve the document");
                std::puts("PASS: original document sample and read-only commands");
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
    int OnRun() override { wxApp::OnRun(); return result_; }
private:
    int result_ = 1;
};
}
wxIMPLEMENT_APP(SmokeApp);
