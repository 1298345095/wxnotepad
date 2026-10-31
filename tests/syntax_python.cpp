#include "editor/editor_control.h"
#include "app/editor_frame.h"
#include <SciLexer.h>
#include <wx/wx.h>
#include <wx/statusbr.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
wxString Filename(const std::filesystem::path& path) {
#ifdef __WXMSW__
    return wxString(path.wstring());
#else
    return wxString::FromUTF8(path.string());
#endif
}
void Paint(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto prefix = std::min(editor->Send(Message::GetLength), intptr_t{128});
    for (int attempt = 0; attempt < 100; ++attempt) {
        wxYield();
        editor->Refresh();
        editor->Update();
        wxYield();
        if (editor->Send(Message::GetEndStyled) >= prefix) return;
        wxMilliSleep(10);
    }
}
void Style(EditorControl* editor, size_t position, int expected) {
    const auto actual = editor->Send(Message::GetStyleIndexAt, position);
    if (actual != expected) {
        std::fprintf(stderr, "position=%zu expected_style=%d actual_style=%zd\n", position, expected, actual);
        throw std::runtime_error("Python syntax differs from pinned lexer at known UTF-8 byte position");
    }
}
void Command(EditorFrame* frame, int id) {
    wxCommandEvent event(wxEVT_MENU, id);
    Require(frame->GetEventHandler()->ProcessEvent(event), "native menu command must be handled");
    Paint(frame);
}
void Choose(EditorFrame* frame, const wxString& label) {
    const int id = frame->GetMenuBar()->FindMenuItem("Language", label);
    Require(id != wxNOT_FOUND, "Language menu must offer Python");
    Command(frame, id);
    auto* item = frame->GetMenuBar()->FindItem(id);
    Require(item->IsRadio() && item->IsChecked(), "chosen language must be checked");
}
void Delete(EditorControl* editor) {
    wxKeyEvent event(wxEVT_KEY_DOWN);
    event.m_keyCode = WXK_DELETE;
    event.SetEventObject(editor);
    editor->GetEventHandler()->ProcessEvent(event);
}
void Character(EditorControl* editor, int character) {
    wxKeyEvent event(wxEVT_CHAR);
    event.m_keyCode = character;
    event.m_uniChar = character;
    event.SetEventObject(editor);
    editor->GetEventHandler()->ProcessEvent(event);
}
std::string Text(EditorControl* editor) {
    std::string text(static_cast<size_t>(editor->Send(Message::GetLength)) + 1, '\0');
    editor->Send(Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    text.pop_back();
    return text;
}
std::string Font(EditorControl* editor, int style) {
    char name[256]{};
    editor->Send(Message::StyleGetFont, style, reinterpret_cast<intptr_t>(name));
    return name;
}
class PythonApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            const auto directory =
                std::filesystem::temp_directory_path() / ("wxnotepad-python-" + std::to_string(wxGetProcessId()));
            try {
                std::filesystem::create_directory(directory);
                const auto path = directory / "example.Py";
                std::ofstream(path, std::ios::binary) << "return 42 # 中文\r\n";
                Require(frame->Open(Filename(path)).status == OpenStatus::Opened, "Python fixture must open");
                Paint(frame);
                Style(frame->Editor(), 0, SCE_PY_WORD);
                Require(frame->GetStatusBar()->GetStatusText() == "UTF-8 | Python | EOL: CRLF",
                        "Open must select Python and retain encoding status");
                std::puts("PASS: Open selects Python and styles keywords");
                auto* editor = frame->Editor();
                const std::string source = "# 中文\r\n"
                                           "class Café:\n"
                                           " def __init__(self):\n"
                                           "  total = len(\"中文\\n\") + 0x2a\n"
                                           "  truth = True\n"
                                           "  name = __name__\n"
                                           "  error = ValueError\n"
                                           "  message = f\"你好 {total:04d}\"\n"
                                           "  doc = \"\"\"中文\ninside\n\"\"\"\n"
                                           "  return int(total)\n";
                const std::string bytes = "\xef\xbb\xbf" + source;
                std::ofstream(path, std::ios::binary) << bytes;
                Require(frame->Open(Filename(path)).status == OpenStatus::Opened, "full Python fixture must open");
                Paint(frame);
                for (const auto& [token, style] : {std::pair{"# 中文", SCE_PY_COMMENTLINE},
                                                   {"class", SCE_PY_WORD},
                                                   {"Café", SCE_PY_CLASS},
                                                   {"__init__", SCE_PY_OBJECT_FUNCTION},
                                                   {"self", SCE_PY_BUILTIN_CONSTANT},
                                                   {"len", SCE_PY_BUILTIN_FUNCTION},
                                                   {"\"中文", SCE_PY_STRING_DQ},
                                                   {"\\n", SCE_PY_ESCAPECHAR},
                                                   {"0x2a", SCE_PY_NUMBER},
                                                   {"True", SCE_PY_BUILTIN_CONSTANT},
                                                   {"__name__", SCE_PY_ATTRIBUTE},
                                                   {"ValueError", SCE_PY_CLASS},
                                                   {"f\"", SCE_PY_FMTSTRING_DQ},
                                                   {"total:04d", SCE_PY_IDENTIFIER},
                                                   {"\"\"\"中文", SCE_PY_TRIPLE_STRING_DQ},
                                                   {"return", SCE_PY_WORD},
                                                   {"int", SCE_PY_WORD2}}) {
                    Style(editor, source.find(token), style);
                }
                Require(editor->Send(Message::StyleGetFore, SCE_PY_WORD) == 0x0080FF &&
                            editor->Send(Message::StyleGetBold, SCE_PY_WORD) &&
                            editor->Send(Message::StyleGetFore, SCE_PY_COMMENTLINE) == 0x608060 &&
                            editor->Send(Message::StyleGetFore, SCE_PY_COMMENTBLOCK) == 0x608060 &&
                            editor->Send(Message::StyleGetFore, SCE_PY_STRING_SQ) == 0x008000 &&
                            editor->Send(Message::StyleGetFore, SCE_PY_STRING_DQ) == 0x008000 &&
                            editor->Send(Message::StyleGetFore, SCE_PY_TRIPLE_STRING_DQ) == 0x0080F0 &&
                            editor->Send(Message::StyleGetFore, SCE_PY_FMTSTRING_DQ) == 0x008064 &&
                            editor->Send(Message::StyleGetFore, SCE_PY_NUMBER) == 0x0000FF,
                        "original Python light palette and grouped styles must be installed");
                for (const int style : {SCE_PY_WORD, SCE_PY_STRING_DQ, SCE_PY_TRIPLE_STRING_DQ, SCE_PY_FMTSTRING_DQ}) {
                    Require(Font(editor, style) == Font(editor, 32) &&
                                editor->Send(Message::StyleGetSizeFractional, style) ==
                                    editor->Send(Message::StyleGetSizeFractional, 32) &&
                                editor->Send(Message::StyleGetBack, style) == 0xFFFFFF,
                            "Python styles must retain native font, size and light background");
                }
                Require(Text(editor) == source && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo) && frame->DocumentHasUtf8Bom(),
                        "highlighting must preserve loaded text, BOM and clean history");
                Require(frame->Save().status == SaveStatus::Saved, "Python must save");
                std::ifstream input(path, std::ios::binary);
                const std::string saved{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
                Require(saved == bytes, "saved BOM, Unicode and mixed EOL bytes must remain exact");
                input.close();
                std::puts("PASS: original consumed keyword groups, palette, Unicode, font and exact saved bytes");

                for (const auto* extension : {"py", "pyw", "pyi", "PY", "PyW", "PyI"}) {
                    const auto candidate = directory / (std::string("file.") + extension);
                    std::ofstream(candidate, std::ios::binary) << "return 42\n";
                    Require(frame->Open(Filename(candidate)).status == OpenStatus::Opened,
                            "Python extension must open");
                    Paint(frame);
                    Style(editor, 0, SCE_PY_WORD);
                }
                const auto originalText = Text(editor);
                editor->Send(Message::SetSel, 2, 5);
                Choose(frame, "Plain Text");
                Style(editor, 0, 0);
                Require(editor->Send(Message::StyleGetFore, SCE_PY_WORD) == 0 &&
                            !editor->Send(Message::StyleGetBold, SCE_PY_WORD),
                        "Plain Text must clear Python palette");
                Choose(frame, "Python");
                Style(editor, 0, SCE_PY_WORD);
                Require(Text(editor) == originalText && editor->Send(Message::GetAnchor) == 2 &&
                            editor->Send(Message::GetCurrentPos) == 5 && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo) &&
                            frame->GetStatusBar()->GetStatusText() == "UTF-8 | Python (override) | EOL: LF",
                        "manual Python choice preserves document state and displays override");
                Require(frame->SaveAs(Filename(directory / "manual.txt"), {}, SaveType::AllFiles).status ==
                            SaveStatus::Saved,
                        "manual Python must save as differently named text");
                Paint(frame);
                Style(editor, 0, 0);
                Require(frame->GetStatusBar()->GetStatusText() == "UTF-8 | Plain Text | EOL: LF" &&
                            frame->GetMenuBar()->IsChecked(frame->GetMenuBar()->FindMenuItem("Language", "Automatic")),
                        "Save As must clear manual Python and identify the .txt filename");
                Choose(frame, "Automatic");
                Style(editor, 0, 0);
                Require(frame->SaveAs(Filename(directory / "automatic.PYI"), {}, SaveType::AllFiles).status ==
                            SaveStatus::Saved,
                        "automatic Save As must select Python");
                Paint(frame);
                Style(editor, 0, SCE_PY_WORD);
                Choose(frame, "C/C++");
                Style(editor, 0, SCE_C_WORD);
                Choose(frame, "Python");
                Style(editor, 0, SCE_PY_WORD);
                Require(frame->Open(std::nullopt).status == OpenStatus::Cancelled &&
                            frame->Open(Filename(directory / "missing.py")).status == OpenStatus::Failed &&
                            frame->GetStatusBar()->GetStatusText() == "UTF-8 | Python (override) | EOL: LF",
                        "failed and cancelled Open must preserve Python override");
                Command(frame, wxID_NEW);
                Character(editor, 'r');
                Choose(frame, "Python");
                Require(frame->GetStatusBar()->GetStatusText() == "UTF-8 | Python (override) | EOL: CRLF",
                        "unnamed text must support manual Python selection");
                Command(frame, wxID_UNDO);
                Require(frame->New(), "clean unnamed document must reset");
                std::puts("PASS: extensions, native menu choices, transitions and Save As language reidentification");

                const std::string triple = "doc = \"\"\"中文\ninside\n\"\"\"\nreturn 42\n";
                std::ofstream(path, std::ios::binary) << triple;
                Require(frame->Open(Filename(path)).status == OpenStatus::Opened, "triple string fixture must open");
                Paint(frame);
                const auto close = triple.rfind("\"\"\"");
                const auto after = triple.find("return");
                Style(editor, after, SCE_PY_WORD);
                editor->Send(Message::GotoPos, close);
                Delete(editor);
                Paint(frame);
                Style(editor, after - 1, SCE_PY_TRIPLE_STRING_DQ);
                Command(frame, wxID_UNDO);
                Style(editor, after, SCE_PY_WORD);
                Require(Text(editor) == triple && !editor->Send(Message::GetModify) && editor->Send(Message::CanRedo),
                        "Undo restores triple-string terminator, bytes and save point");
                Command(frame, wxID_REDO);
                Style(editor, after - 1, SCE_PY_TRIPLE_STRING_DQ);
                editor->Send(Message::GotoPos, close);
                Character(editor, '"');
                Paint(frame);
                Style(editor, after, SCE_PY_WORD);
                while (editor->Send(Message::CanUndo))
                    Command(frame, wxID_UNDO);
                Require(Text(editor) == triple && !editor->Send(Message::GetModify),
                        "recoloring must not create history entries");

                const std::string formatted = "message = f\"你好 {len('中文')}\"\nreturn 42\n";
                std::ofstream(path, std::ios::binary) << formatted;
                Require(frame->Open(Filename(path)).status == OpenStatus::Opened, "formatted fixture must open");
                Paint(frame);
                const auto prefix = formatted.find("f\"");
                const auto expression = formatted.find("len");
                Style(editor, expression, SCE_PY_BUILTIN_FUNCTION);
                editor->Send(Message::GotoPos, prefix);
                Delete(editor);
                Paint(frame);
                Style(editor, expression - 1, SCE_PY_STRING_DQ);
                Command(frame, wxID_UNDO);
                Style(editor, expression, SCE_PY_BUILTIN_FUNCTION);
                Require(Text(editor) == formatted && !editor->Send(Message::GetModify),
                        "Undo restores formatted expression and save point");
                Command(frame, wxID_REDO);
                Style(editor, expression - 1, SCE_PY_STRING_DQ);
                editor->Send(Message::GotoPos, prefix);
                Character(editor, 'f');
                Paint(frame);
                Style(editor, expression, SCE_PY_BUILTIN_FUNCTION);
                while (editor->Send(Message::CanUndo))
                    Command(frame, wxID_UNDO);
                Require(Text(editor) == formatted && !editor->Send(Message::GetModify),
                        "formatted-string edits retain exact UTF-8 bytes and history");
                std::puts("PASS: real Delete/typing/Undo/Redo update triple and formatted string spans");
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
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

private:
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(PythonApp);
