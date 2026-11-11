#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "syntax_test_helpers.h"
#include "file_helpers.h"
#include "settings/settings_store.h"
#include <SciLexer.h>
#include <wx/wx.h>
#include <cstdio>
#include <sstream>
#include <fstream>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw TestEditor::Failure(message);
}
std::string Unhex(const std::string& hex) {
    std::string bytes;
    for (size_t index = 0; index < hex.size(); index += 2)
        bytes += static_cast<char>(std::stoi(hex.substr(index, 2), nullptr, 16));
    return bytes;
}
void OriginalOracle(EditorFrame* frame) {
    std::ifstream input(std::filesystem::path(NATIVE_LANGUAGES_REFERENCE));
    Require(input.is_open(), "independently captured original style/fold fixture must open");
    std::string line;
    size_t rows = 0;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') continue;
        std::istringstream row(line);
        std::string name, state, deleted, textHex, styleHex, foldText;
        std::getline(row, name, '\t');
        std::getline(row, state, '\t');
        std::getline(row, deleted, '\t');
        std::getline(row, textHex, '\t');
        std::getline(row, styleHex, '\t');
        std::getline(row, foldText);
        auto* editor = frame->Editor();
        const auto text = Unhex(textHex);
        if (state == "base") {
            editor->Send(Message::SetSavePoint);
            const auto path = TestFiles::TemporaryDirectory(name);
            TestFiles::WriteBytes(path, text);
            Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened,
                    "original Rust/Go/Lua fixture Open must succeed");
        } else if (state == "edit") {
            editor->Send(Message::GotoPos, std::stoul(deleted));
            wxKeyEvent event(wxEVT_KEY_DOWN);
            event.m_keyCode = WXK_DELETE;
            event.SetEventObject(editor);
            editor->GetEventHandler()->ProcessEvent(event);
        } else {
            TestSyntax::Command(frame, state == "undo" ? wxID_UNDO : wxID_REDO);
        }
        TestSyntax::Paint(frame);
        Require(TestEditor::Text(editor) == text, "incremental edit/Undo/Redo bytes must match the original");
        const auto styles = Unhex(styleHex);
        Require(styles.size() == text.size(), "original styles must cover every UTF-8 byte");
        for (size_t position = 0; position < styles.size(); ++position) {
            if (editor->Send(Message::GetStyleIndexAt, position) != static_cast<unsigned char>(styles[position])) {
                std::fprintf(stderr, "fixture=%s state=%s position=%zu expected=%u actual=%zd\n", name.c_str(),
                             state.c_str(), position, static_cast<unsigned char>(styles[position]),
                             editor->Send(Message::GetStyleIndexAt, position));
                throw std::runtime_error("Rust/Go/Lua styles must match independent original observations");
            }
        }
        std::istringstream folds(foldText);
        size_t foldLine = 0;
        while (std::getline(folds, line, ',')) {
            if (editor->Send(Message::GetFoldLevel, foldLine) != std::stoi(line)) {
                std::fprintf(stderr, "fixture=%s state=%s line=%zu expected=%d actual=%zd\n", name.c_str(),
                             state.c_str(), foldLine, std::stoi(line), editor->Send(Message::GetFoldLevel, foldLine));
                throw std::runtime_error("Rust/Go/Lua folds must match independent original observations");
            }
            ++foldLine;
        }
        ++rows;
    }
    Require(rows == 12, "all three original fixtures and four editing states must run");
    std::puts("PASS: 12 independent original Rust/Go/Lua style/fold states including incremental edit and Undo/Redo");
}
class LanguagesApp final : public wxApp {
public:
    bool OnInit() override {
        wxInitAllImageHandlers();
        auto* frame = new EditorFrame;
        frame->Show();
        CallAfter([this, frame] {
            try {
                OriginalOracle(frame);
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s", error.what());
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

private:
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(LanguagesApp);
