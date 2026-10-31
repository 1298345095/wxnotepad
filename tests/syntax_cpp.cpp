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
    const auto visiblePrefix = std::min(editor->Send(Message::GetLength), intptr_t{128});
    // Wayland schedules mapping and frame callbacks asynchronously. Observe a
    // natural visible style pass, never force Colourise from the test.
    for (int attempt = 0; attempt < 100; ++attempt) {
        wxYield();
        editor->Refresh();
        editor->Update();
        wxYield();
        if (editor->Send(Message::GetEndStyled) >= visiblePrefix) return;
        wxMilliSleep(10);
    }
}
void Command(EditorFrame* frame, int id) {
    wxCommandEvent event(wxEVT_MENU, id);
    Require(frame->GetEventHandler()->ProcessEvent(event), "real editor command must be handled");
    Paint(frame);
}
void Character(EditorControl* editor, int character) {
    wxKeyEvent event(wxEVT_CHAR);
    event.m_keyCode = character;
    event.m_uniChar = character;
    event.SetEventObject(editor);
    editor->GetEventHandler()->ProcessEvent(event);
}
void Delete(EditorControl* editor) {
    wxKeyEvent event(wxEVT_KEY_DOWN);
    event.m_keyCode = WXK_DELETE;
    event.SetEventObject(editor);
    editor->GetEventHandler()->ProcessEvent(event);
}
std::string Text(EditorControl* editor) {
    const auto length = static_cast<size_t>(editor->Send(Message::GetLength));
    std::string text(length + 1, '\0');
    editor->Send(Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    text.resize(length);
    return text;
}
void Style(EditorControl* editor, size_t position, int expected) {
    const auto actual = editor->Send(Message::GetStyleIndexAt, position);
    if (actual != expected) {
        std::fprintf(stderr, "position=%zu expected_style=%d actual_style=%zd\n", position, expected, actual);
        throw std::runtime_error("syntax style at known UTF-8 byte position differs from pinned lexer");
    }
}
std::string Font(EditorControl* editor, int style) {
    char font[256]{};
    editor->Send(Message::StyleGetFont, style, reinterpret_cast<intptr_t>(font));
    return font;
}
class SyntaxApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            const auto directory =
                std::filesystem::temp_directory_path() / ("wxnotepad-cpp-" + std::to_string(wxGetProcessId()));
            try {
                std::filesystem::create_directory(directory);
                const auto path = directory / "example.CPP";
                std::ofstream(path, std::ios::binary) << "int value = 42; // 中文\r\n";
                Require(frame->Open(Filename(path)).status == OpenStatus::Opened, "C++ file must open");
                Paint(frame);
                Require(frame->Editor()->Send(Scintilla::Message::GetStyleIndexAt, 0) == SCE_C_WORD2,
                        "opening .CPP must immediately highlight int as a type keyword");
                Require(frame->GetStatusBar()->GetStatusText().Contains("C/C++"),
                        "status must show effective C/C++ language");
                std::puts("PASS: Open selects C++ and styles type keywords");

                auto* editor = frame->Editor();
                const std::string source = "#define VALUE 42\r\n"
                                           "int value = 42; // 中文\r\n"
                                           "/* 中文 block */\n"
                                           "/** @param name 中文 */\n"
                                           "const char* text = \"中文\\n\";\n"
                                           "char letter = 'x';\n"
                                           "auto raw = R\"(中文)\";\n"
                                           "return value;\n";
                const std::string bytes = "\xef\xbb\xbf" + source;
                std::ofstream(path, std::ios::binary) << bytes;
                Require(frame->Open(Filename(path)).status == OpenStatus::Opened, "syntax fixture must open");
                Paint(frame);
                for (const auto& [token, style] : {std::pair{"#define", SCE_C_PREPROCESSOR},
                                                   {"42", SCE_C_NUMBER},
                                                   {"int", SCE_C_WORD2},
                                                   {"//", SCE_C_COMMENTLINE},
                                                   {"/* 中文", SCE_C_COMMENT},
                                                   {"/**", SCE_C_COMMENTDOC},
                                                   {"@param", SCE_C_COMMENTDOC_TAG},
                                                   {"const", SCE_C_WORD},
                                                   {"\"中文", SCE_C_STRING},
                                                   {"\\n", SCE_C_ESCAPECHAR},
                                                   {"'x'", SCE_C_CHARACTER},
                                                   {"R\"", SCE_C_STRINGRAW},
                                                   {"return", SCE_C_WORD}}) {
                    Style(editor, source.find(token), style);
                }
                Style(editor, source.find("中文"), SCE_C_COMMENTLINE);
                Style(editor, source.find("中文", source.find("const")), SCE_C_STRING);
                Require(editor->Send(Message::StyleGetFore, SCE_C_WORD) == 0xFF0000 &&
                            editor->Send(Message::StyleGetFore, SCE_C_2NDWORD) == 0xFF0000 &&
                            editor->Send(Message::StyleGetFore, SCE_C_COMMENT) == 0x608060 &&
                            editor->Send(Message::StyleGetFore, SCE_C_COMMENTLINE) == 0x608060 &&
                            editor->Send(Message::StyleGetFore, SCE_C_COMMENTLINEDOC) == 0x408040 &&
                            editor->Send(Message::StyleGetFore, SCE_C_COMMENTDOC_TAG_XML) == 0x408040 &&
                            editor->Send(Message::StyleGetFore, SCE_C_STRING) == 0x008000 &&
                            editor->Send(Message::StyleGetFore, SCE_C_CHARACTER) == 0x008000 &&
                            editor->Send(Message::StyleGetFore, SCE_C_STRINGEOL) == 0x008000 &&
                            editor->Send(Message::StyleGetFore, SCE_C_NUMBER) == 0x0000FF &&
                            editor->Send(Message::StyleGetFore, SCE_C_PREPROCESSOR) == 0x0080FF &&
                            editor->Send(Message::StyleGetFore, SCE_C_MACRO) == 0xB000B0 &&
                            editor->Send(Message::StyleGetFore, SCE_C_MACRO2) == 0xB000B0 &&
                            editor->Send(Message::StyleGetBack, SCE_C_LABEL) == 0x40C0FF &&
                            editor->Send(Message::StyleGetBold, SCE_C_INTERFACE),
                        "original default palette and all grouped styles must be installed");
                for (const int style : {SCE_C_WORD, SCE_C_COMMENT, SCE_C_STRING, SCE_C_NUMBER, SCE_C_PREPROCESSOR}) {
                    Require(Font(editor, style) == Font(editor, 32) &&
                                editor->Send(Message::StyleGetSizeFractional, style) ==
                                    editor->Send(Message::StyleGetSizeFractional, 32) &&
                                editor->Send(Message::StyleGetBack, style) == 0xFFFFFF,
                            "syntax must inherit the native font and point size on explicit light background");
                }
                Require(Text(editor) == source && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo) && frame->DocumentHasUtf8Bom() &&
                            frame->GetStatusBar()->GetStatusText() == "UTF-8 (BOM) | C/C++",
                        "styling must preserve loaded text, clean save point, encoding and BOM status");
                Require(frame->Save().status == SaveStatus::Saved, "highlighted document must save");
                std::ifstream input(path, std::ios::binary);
                const std::string saved{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
                Require(saved == bytes, "syntax must preserve independently read BOM, Unicode and mixed EOL bytes");
                input.close();
                std::puts("PASS: original token styles, grouped palette, native font and exact saved bytes");

                for (const auto* extension : {"c", "cpp", "cxx", "cc", "h", "hpp", "hxx", "hh", "C", "CpP", "CXX", "CC",
                                              "H", "HpP", "HXX", "HH"}) {
                    const auto candidate = directory / (std::string("file.") + extension);
                    std::ofstream(candidate, std::ios::binary) << "int value;\n";
                    Require(frame->Open(Filename(candidate)).status == OpenStatus::Opened,
                            "each supported ASCII extension must open");
                    Paint(frame);
                    Style(editor, 0, SCE_C_WORD2);
                }
                Require(frame->Open(std::nullopt).status == OpenStatus::Cancelled &&
                            frame->Open(Filename(directory / "missing.txt")).status == OpenStatus::Failed &&
                            frame->GetStatusBar()->GetStatusText().Contains("C/C++"),
                        "Open cancellation and failure must retain previous syntax");
                Paint(frame);
                Style(editor, 0, SCE_C_WORD2);
                const auto plain = directory / "unknown.bin";
                std::ofstream(plain, std::ios::binary) << "int value;\n";
                Require(frame->Open(Filename(plain)).status == OpenStatus::Opened, "unknown file must open");
                Paint(frame);
                Style(editor, 0, 0);
                Require(frame->GetStatusBar()->GetStatusText().Contains("Plain Text") &&
                            editor->Send(Message::StyleGetFore, SCE_C_WORD2) == 0 &&
                            !editor->Send(Message::StyleGetBold, SCE_C_INTERFACE),
                        "Plain Text must clear syntax colors and font attributes");
                Require(frame->Open(Filename(path)).status == OpenStatus::Opened, "C++ must reopen");
                Command(frame, wxID_NEW);
                Require(Text(editor).empty() && frame->DocumentPath().empty() && !editor->Send(Message::CanUndo) &&
                            !editor->Send(Message::GetModify) &&
                            frame->GetStatusBar()->GetStatusText() == "UTF-8 | Plain Text",
                        "real New command must reset language with the existing empty-document semantics");
                std::puts("PASS: case-insensitive extension selection, failed/cancelled Open and real New");

                std::string comment = "/* 中文\nmiddle\n*/\nint after = 1;\n";
                for (int line = 0; line < 5000; ++line)
                    comment += "int untouched;\n";
                std::ofstream(path, std::ios::binary) << comment;
                Require(frame->Open(Filename(path)).status == OpenStatus::Opened, "multiline fixture must open");
                Paint(frame);
                const auto close = comment.find("*/");
                const auto after = comment.find("int");
                Style(editor, after, SCE_C_WORD2);
                editor->Send(Message::GotoPos, close);
                Delete(editor);
                Require(editor->Send(Message::GetEndStyled) < editor->Send(Message::GetLength),
                        "typing must leave recoloring to the original incremental styling lifecycle");
                Paint(frame);
                Style(editor, after - 1, SCE_C_COMMENT);
                Require(editor->Send(Message::GetCurrentPos) == static_cast<intptr_t>(close) &&
                            editor->Send(Message::GetAnchor) == static_cast<intptr_t>(close) &&
                            editor->Send(Message::CanUndo) && editor->Send(Message::GetModify),
                        "recolor after deleting a comment terminator must preserve caret and edit history");
                Command(frame, wxID_UNDO);
                Style(editor, after, SCE_C_WORD2);
                Require(Text(editor) == comment && !editor->Send(Message::GetModify) && editor->Send(Message::CanRedo),
                        "Undo must restore closing comment and original save point");
                Command(frame, wxID_REDO);
                Style(editor, after - 1, SCE_C_COMMENT);
                editor->Send(Message::GotoPos, close);
                Character(editor, '*');
                Paint(frame);
                Style(editor, after, SCE_C_WORD2);
                Require(Text(editor) == comment, "typing a comment terminator must restore following syntax");
                Command(frame, wxID_UNDO);
                Style(editor, after - 1, SCE_C_COMMENT);
                Command(frame, wxID_REDO);
                Style(editor, after, SCE_C_WORD2);
                while (editor->Send(Message::CanUndo))
                    Command(frame, wxID_UNDO);
                Require(Text(editor) == comment && !editor->Send(Message::GetModify),
                        "style updates must not add undo actions or move the save point");
                std::puts("PASS: real typing/Delete/Undo/Redo update following multiline-comment styles");
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
wxIMPLEMENT_APP(SyntaxApp);
