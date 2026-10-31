#include "native_search_helpers.h"
#include "native_fold_helpers.h"
#include <wx/statusbr.h>
#include <filesystem>
#include <fstream>

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Convert(EditorFrame* frame, const char* mode) {
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", wxString("Convert to ") + mode));
}
void Load(EditorControl* editor, const char* text) {
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(text));
    editor->Send(Message::EmptyUndoBuffer);
    editor->Send(Message::SetSavePoint);
}
void Enter(EditorControl* editor) {
#ifdef __WXMSW__
    SendMessageW(static_cast<HWND>(editor->GetHandle()), WM_KEYDOWN, VK_RETURN, 0);
#elif defined(__WXGTK__)
    auto* event = gdk_event_new(GDK_KEY_PRESS);
    event->key.window = static_cast<GdkWindow*>(g_object_ref(editor->GTKGetDrawingWindow()));
    event->key.send_event = TRUE;
    event->key.keyval = GDK_KEY_Return;
    event->key.hardware_keycode = 36;
    gtk_widget_event(editor->GetConnectWidget(), event);
    gdk_event_free(event);
#endif
    wxYield();
}
void History(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const std::string source = "中文\r\nsecond\nthird\rlast";
    Load(editor, source.c_str());
    Convert(frame, "LF");
    Require(editor->Send(Message::GetModify), "a text-changing conversion must dirty the document");
    NativeSearch::Command(frame, wxID_UNDO);
    Require(NativeSearch::Text(editor) == source && !editor->Send(Message::GetModify) &&
                !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
            "one Undo must restore exact mixed bytes and the original save point");
    Require(editor->Send(Message::GetEOLMode) == static_cast<intptr_t>(Scintilla::EndOfLine::Lf) &&
                frame->GetStatusBar()->GetStatusText().Contains("EOL: LF"),
            "Undo must retain the chosen insertion preference and truthful status");
    Convert(frame, "LF");
    Require(NativeSearch::Text(editor) == "中文\nsecond\nthird\nlast",
            "selecting the already selected mode must normalize mixed bytes restored by Undo");
    NativeSearch::Command(frame, wxID_UNDO);
    NativeSearch::Command(frame, wxID_REDO);
    Require(NativeSearch::Text(editor) == "中文\nsecond\nthird\nlast" && editor->Send(Message::GetModify),
            "Redo must restore the converted bytes");
    editor->Send(Message::GotoPos, editor->Send(Message::GetLength));
    Enter(editor);
    Require(NativeSearch::Text(editor) == "中文\nsecond\nthird\nlast\n",
            "native Enter must use the explicitly selected insertion mode");
    for (const auto* text : {"", "中文", "one\ntwo"}) {
        Load(editor, text);
        editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
        editor->Send(Message::Undo);
        Convert(frame, "LF");
        Require(NativeSearch::Text(editor) == text && !editor->Send(Message::GetModify) &&
                    !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
                "no-op conversion must preserve save point and pending redo for empty, single and normalized text");
        editor->Send(Message::Redo);
        Require(NativeSearch::Text(editor) == std::string(text) + "!", "no-op conversion must retain redo bytes");
    }
    std::puts("PASS: single-action history, unchanged insertion preference, native Enter and no-op Redo");
}
void FoldedSelection(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const int language = frame->GetMenuBar()->FindMenuItem("Language", "C/C++");
    NativeSearch::Command(frame, language);
    std::string source = "void outer() {\r\n  中文(); //";
    for (int word = 0; word < 100; ++word)
        source += " wrapped";
    source += "\r\n}\r\nafter();";
    Load(editor, source.c_str());
    editor->ToggleWordWrap();
    NativeFold::Paint(editor);
    Require(editor->Send(Message::WrapCount, 1) > 1, "fixture must wrap the selected logical line");
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Collapse All"));
    Require(!editor->Send(Message::GetLineVisible, 1), "fixture must hide the inner logical line");
    editor->Send(Message::SetSel, 24, 18);
    Convert(frame, "LF");
    Require(editor->Send(Message::GetAnchor) == 23 && editor->Send(Message::GetCurrentPos) == 17,
            "conversion must preserve reversed selection at logical Unicode character boundaries");
    const auto current = editor->Send(Message::GetCurrentPos);
    const auto currentLine = editor->Send(Message::LineFromPosition, current);
    const auto y = editor->Send(Message::PointYFromPosition, 0, current);
    Require(editor->Send(Message::GetLineVisible, currentLine) && y >= 0 && y < editor->GetClientSize().y,
            "conversion must reveal its logical caret inside a wrapped folded block");
    Require(frame->GetMenuBar()->FindItem(language)->IsChecked(), "conversion must retain manual syntax choice");
    editor->ToggleWordWrap();
    std::puts("PASS: reversed Unicode selection and caret visibility through wrapped folded conversion");
}
wxString Filename(const std::filesystem::path& path) {
#ifdef __WXMSW__
    return wxString(path.wstring());
#else
    return wxString::FromUTF8(path.string());
#endif
}
std::string Bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    Require(input.is_open(), "saved file must exist");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void RecordBytes(const char* sample, const char* action, const std::filesystem::path& path) {
    std::printf("BYTES %s %s:", sample, action);
    for (unsigned char byte : Bytes(path))
        std::printf(" %02x", byte);
    std::puts("");
}
void Files(EditorFrame* frame, const std::filesystem::path& directory) {
    using namespace std::string_literals;
    auto* editor = frame->Editor();
    struct Sample {
        const char* name;
        std::string source;
        std::string expected;
        std::optional<FileEncoding> encoding = std::nullopt;
    };
    const Sample samples[] = {
        {"utf8", "中文\r\nnext\rlast", "中文\nnext\nlast"},
        {"utf8-bom", "\xef\xbb\xbf中文\r\nnext\rlast", "\xef\xbb\xbf中文\nnext\nlast"},
        {"utf16-bom", "\xff\xfe\x2d\x4e\r\0\n\0n\0\r\0z\0"s, "\xff\xfe\x2d\x4e\n\0n\0\n\0z\0"s},
        {"utf16-no-bom", "\x2d\x4e\r\0\n\0n\0\r\0z\0"s, "\x2d\x4e\n\0n\0\n\0z\0"s, FileEncoding::Utf16Le},
        {"gbk", "\xd6\xd0\r\nnext\rlast", "\xd6\xd0\nnext\nlast", FileEncoding::Gbk},
    };
    std::filesystem::create_directory(directory);
    for (const auto& sample : samples) {
        const auto input = directory / (std::string(sample.name) + ".txt");
        const auto output = directory / (std::string(sample.name) + "-as.txt");
        {
            std::ofstream file(input, std::ios::binary);
            file << sample.source;
        }
        editor->Send(Message::SetSavePoint);
        Require(frame->Open(Filename(input), sample.encoding).status == OpenStatus::Opened,
                "encoding fixture must open through the application");
        const auto encoding = frame->DocumentEncoding();
        const auto bom = frame->DocumentHasBom();
        Convert(frame, "LF");
        Require(Bytes(input) == sample.source && frame->DocumentPath() == Filename(input) &&
                    frame->DocumentEncoding() == encoding && frame->DocumentHasBom() == bom,
                "conversion must retain identity and encoding and must not write before Save");
        Require(frame->SaveAs(std::nullopt).status == SaveStatus::Cancelled &&
                    frame->SaveAs(Filename(directory / "missing" / "file.txt")).status == SaveStatus::Failed,
                "cancelled and failed Save As must retain the converted document");
        Require(editor->Send(Message::GetEOLMode) == static_cast<intptr_t>(Scintilla::EndOfLine::Lf) &&
                    frame->DocumentPath() == Filename(input) && editor->Send(Message::GetModify),
                "failed and cancelled save must retain insertion preference, identity and dirty state");
        Require(frame->Save().status == SaveStatus::Saved && Bytes(input) == sample.expected,
                "Save must write independently verified converted encoding bytes without a final newline");
        RecordBytes(sample.name, "converted-save", input);
        NativeSearch::Command(frame, wxID_UNDO);
        Require(editor->Send(Message::GetModify) && frame->SaveAs(Filename(output)).status == SaveStatus::Saved &&
                    Bytes(output) == sample.source,
                "Undo across a saved conversion must write the exact original bytes through Save As");
        RecordBytes(sample.name, "undo-save-as", output);
        NativeSearch::Command(frame, wxID_REDO);
        Require(editor->Send(Message::GetModify) && frame->Save().status == SaveStatus::Saved &&
                    Bytes(output) == sample.expected,
                "Redo across the Undo save point must save converted bytes again");
        NativeSearch::Command(frame, wxID_UNDO);
        Require(editor->Send(Message::GetModify), "Undo away from converted save point must dirty text");
        NativeSearch::Command(frame, wxID_REDO);
        Require(!editor->Send(Message::GetModify), "Redo to converted save point must restore clean state");
        std::printf("PASS: %s Save/Save As independent bytes, conversion/Undo/Redo save points\n", sample.name);
    }
    Require(frame->Open(std::nullopt).status == OpenStatus::Cancelled &&
                frame->Open(Filename(directory / "absent.txt")).status == OpenStatus::Failed &&
                editor->Send(Message::GetEOLMode) == static_cast<intptr_t>(Scintilla::EndOfLine::Lf) &&
                NativeSearch::Text(editor) == "中\nnext\nlast",
            "cancelled and failed Open must retain converted text and insertion preference");
    Require(frame->New() && editor->Send(Message::GetEOLMode) == static_cast<intptr_t>(Scintilla::EndOfLine::CrLf) &&
                NativeSearch::Text(editor).empty() && frame->GetStatusBar()->GetStatusText().Contains("EOL: CRLF"),
            "successful New must restore the existing CRLF default and status");
    std::puts("PASS: cancelled/failed file operations retain mode; New restores the original default");
}
void SearchInvalidation(EditorFrame* frame) {
    auto* editor = frame->Editor();
    Load(editor, "a\r\nb\r\nc");
    editor->Send(Message::GotoPos, 0);
    SearchSession search(*editor);
    SearchRequest request;
    request.query = "(b)";
    request.replacement = "<$1>";
    request.options.regularExpression = true;
    search.Configure(request);
    Require(search.Find(SearchDirection::Forward).status == FindStatus::Found,
            "the real matcher must select an initial regex capture");
    Convert(frame, "LF");
    Require(!search.ReplaceOne().changed && NativeSearch::Text(editor) == "a\nb\nc",
            "conversion must invalidate a previously selected regex capture before Replace");
    Require(search.ReplaceOne().changed && NativeSearch::Text(editor) == "a\n<b>\nc",
            "a fresh match after conversion must replace using current capture bytes");
    std::puts("PASS: text-changing conversion invalidates search captures and permits a fresh replacement");
}
class EolConversionApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            const auto directory = std::filesystem::temp_directory_path() /
                                   ("wxnotepad-eol-conversion-" + std::to_string(wxGetProcessId()));
            try {
                auto* editor = frame->Editor();
                struct Mode {
                    const char* label;
                    const char* expected;
                    Scintilla::EndOfLine mode;
                };
                const Mode modes[] = {{"CRLF", "中文\r\nsecond\r\nthird\r\nlast", Scintilla::EndOfLine::CrLf},
                                      {"LF", "中文\nsecond\nthird\nlast", Scintilla::EndOfLine::Lf},
                                      {"CR", "中文\rsecond\rthird\rlast", Scintilla::EndOfLine::Cr}};
                for (const auto& mode : modes) {
                    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("中文\r\nsecond\nthird\rlast"));
                    editor->Send(Message::EmptyUndoBuffer);
                    editor->Send(Message::SetSavePoint);
                    const int id = frame->GetMenuBar()->FindMenuItem("Edit", wxString("Convert to ") + mode.label);
                    Require(id != wxNOT_FOUND, "Edit must offer CRLF, LF and CR conversion commands");
                    NativeSearch::Command(frame, id);
                    if (NativeSearch::Text(editor) != mode.expected)
                        std::fprintf(stderr, "Mode %s actual text: %s\n", mode.label,
                                     NativeSearch::Text(editor).c_str());
                    Require(NativeSearch::Text(editor) == mode.expected,
                            "native conversion must normalize mixed endings without adding a final newline");
                    Require(editor->Send(Message::GetEOLMode) == static_cast<intptr_t>(mode.mode),
                            "conversion must choose the original core insertion mode");
                    Require(frame->GetStatusBar()->GetStatusText().Contains(wxString("EOL: ") + mode.label),
                            "status must explicitly identify the insertion mode");
                    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                    Require(frame->GetMenuBar()->FindItem(id)->IsChecked(), "chosen conversion must be checked");
                    for (const auto& other : modes) {
                        const int otherId =
                            frame->GetMenuBar()->FindMenuItem("Edit", wxString("Convert to ") + other.label);
                        Require(otherId != wxNOT_FOUND && frame->GetMenuBar()->FindItem(otherId)->IsCheckable() &&
                                    frame->GetMenuBar()->FindItem(otherId)->IsChecked() == (other.mode == mode.mode),
                                "conversion commands must be mutually exclusive checked items");
                    }
                    editor->Send(Message::GotoPos, editor->Send(Message::GetLength));
                    Enter(editor);
                    const char* ending = mode.mode == Scintilla::EndOfLine::CrLf ? "\r\n"
                                         : mode.mode == Scintilla::EndOfLine::Cr ? "\r"
                                                                                 : "\n";
                    Require(NativeSearch::Text(editor) == std::string(mode.expected) + ending,
                            "native Enter must insert each chosen line ending");
                    std::printf("PASS: native %s conversion, insertion mode and status\n", mode.label);
                }
                History(frame);
                FoldedSelection(frame);
                Files(frame, directory);
                SearchInvalidation(frame);
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
    int OnExit() override { return result_; }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(EolConversionApp);
